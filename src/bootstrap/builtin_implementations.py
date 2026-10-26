def abs(obj):
    """Return the absolute value of the argument."""
    return __clover_call_special__(
        obj, "__abs__", TypeError, "bad operand type for abs()"
    )


def iter(obj):
    """iter(iterable) -> iterator
iter(callable, sentinel) -> iterator

Get an iterator from an object.  In the first form, the argument must
supply its own iterator, or be a sequence.
In the second form, the callable is called until it returns the sentinel."""
    return __clover_call_special__(
        obj, "__iter__", TypeError, "object is not iterable"
    )


def next(obj, *default):
    """next(iterator[, default])

Return the next item from the iterator. If default is given and the iterator
is exhausted, it is returned instead of raising StopIteration."""
    if len(default) > 1:
        raise TypeError
    try:
        return __clover_call_special__(
            obj, "__next__", TypeError, "object is not an iterator"
        )
    except StopIteration:
        if len(default) == 0:
            raise
        return default[0]


def repr(obj):
    """Return the canonical string representation of the object.

For many object types, including most builtins, eval(repr(obj)) == obj."""
    return __clover_call_special__(
        obj, "__repr__", TypeError, "object has no __repr__"
    )


def hash(obj):
    """Return the hash value for the object."""
    return __clover_canonicalize_hash__(
        __clover_call_special__(
            obj, "__hash__", TypeError, "object is unhashable"
        )
    )


def len(obj):
    """Return the number of items in a container."""
    return _clover_builtin_validate_length(
        __clover_call_special__(
            obj, "__len__", TypeError, "object has no len()"
        )
    )


def getattr(obj, name, *default):
    """Return the named attribute from an object.

If a default is given, it is returned when the attribute is missing."""
    return _clover_builtin_getattr_default(obj, name, default)


def hasattr(obj, name):
    """Return whether the object has the named attribute."""
    return _clover_builtin_hasattr(obj, name)


def vars(*obj):
    """Return __dict__ for an object, or locals() with no argument."""
    if len(obj) == 0:
        return locals()
    if len(obj) > 1:
        raise TypeError
    return _clover_builtin_getattr(obj[0], "__dict__")


def dir(*obj):
    """Return an alphabetized list of names in the local scope or on an object."""
    if len(obj) == 0:
        return _clover_builtin_dir(locals())
    if len(obj) > 1:
        raise TypeError
    return _clover_builtin_dir(obj[0])


def globals():
    """Return the dictionary containing the current scope's global variables."""
    return __clover_globals__()


def locals():
    """Return a dictionary containing the current scope's local variables."""
    return __clover_locals__()


def print(*args, sep=" ", end="\n", file=None, flush=False):
    """print(*args, sep=' ', end='\\n', file=None, flush=False)

Print the values to standard output."""
    if sep is None:
        sep = " "
    elif not isinstance(sep, str):
        raise TypeError
    if end is None:
        end = "\n"
    elif not isinstance(end, str):
        raise TypeError
    if file is not None:
        raise TypeError
    first = True
    for obj in args:
        if first:
            first = False
        else:
            __clover_write_stdout__(sep)
        __clover_write_stdout__(
            __clover_call_special__(
                obj, "__str__", TypeError, "object has no __str__"
            )
        )
    __clover_write_stdout__(end)


def pow(a, b, modulo=None):
    """Return a raised to the power b, optionally reduced modulo modulo."""
    if modulo is None:
        return a**b
    return __clover_ternary_pow__(a, b, modulo)


def sum(iterable, start=0):
    """Return the sum of a 'start' value (default: 0) plus an iterable of numbers.

When the iterable is empty, return the start value.
This function is intended specifically for use with numeric values and may
reject non-numeric types."""
    total = start
    for item in iterable:
        total = total + item
    return total


def any(iterable):
    """Return True if bool(x) is True for any x in the iterable.

If the iterable is empty, return False."""
    for item in iterable:
        if item:
            return True
    return False


def all(iterable):
    """Return True if bool(x) is True for all values x in the iterable.

If the iterable is empty, return True."""
    for item in iterable:
        if not item:
            return False
    return True


def _clover_dict_next(iterator):
    return __clover_call_special__(
        iterator, "__next__", TypeError, "object is not an iterator"
    )


def _clover_dict_update(self, *args, **kwargs):
    if not isinstance(self, dict):
        raise TypeError
    if len(args) > 1:
        raise TypeError
    if len(args) == 1:
        source = args[0]
        if hasattr(source, "keys"):
            for key in source.keys():
                dict.__setitem__(self, key, source[key])
        else:
            for element in source:
                element_iterator = iter(element)
                try:
                    key = _clover_dict_next(element_iterator)
                    value = _clover_dict_next(element_iterator)
                except StopIteration:
                    raise ValueError
                try:
                    _clover_dict_next(element_iterator)
                except StopIteration:
                    dict.__setitem__(self, key, value)
                else:
                    raise ValueError
    for key in kwargs.keys():
        dict.__setitem__(self, key, kwargs[key])


def _clover_dict_new(cls, *args, **kwargs):
    if len(args) > 1:
        raise TypeError
    # Preserve the existing exact-dict keyword-only allocation fast path.
    if cls is dict and len(args) == 0:
        return kwargs
    result = _clover_dict_allocate(cls)
    if len(args) == 1:
        _clover_dict_update(result, args[0])
    _clover_dict_update(result, kwargs)
    return result


def _clover_iter_membership_fallback(container, needle):
    for item in container:
        if item == needle:
            return True
    return False


def _clover_sequence_membership_fallback(container, needle):
    idx = 0
    while True:
        try:
            item = container[idx]
        except IndexError:
            return False
        except StopIteration:
            return False
        if item == needle:
            return True
        idx += 1


def sorted(*args, **kwargs):
    """Return a new, stably sorted list from the iterable.

sorted(iterable, /, *, key=None, reverse=False)"""
    if len(args) != 1:
        raise TypeError("sorted expected 1 argument, got " + str(len(args)))
    # Like CPython, consume the source before list.sort validates keywords.
    result = _clover_iterable_to_exact_list(args[0])
    options = _clover_selection_keywords(kwargs, "sort", "reverse", False)
    _clover_list_sort(result, key=options[0], reverse=options[1])
    return result


def min(*args, **kwargs):
    """Return the smallest item, retaining the first item on ties.

min(iterable, /, *, key=None, default=...) or min(arg1, arg2, /, *args, key=None)"""
    return _clover_min_max(args, kwargs, False)


def max(*args, **kwargs):
    """Return the largest item, retaining the first item on ties.

max(iterable, /, *, key=None, default=...) or max(arg1, arg2, /, *args, key=None)"""
    return _clover_min_max(args, kwargs, True)


def _clover_selection_keywords(kwargs, name, second_name, second_default):
    # Builtin keyword matching uses string payloads, unlike Python call binding.
    # Read stored entries without invoking keyword hash/equality hooks again.
    if len(kwargs) > 2:
        raise TypeError(
            name + "() takes at most 2 keyword arguments (" + str(len(kwargs)) + " given)"
        )
    key = None
    has_key = False
    second = second_default
    has_second = False
    invalid = False
    entries = kwargs.items()
    for entry in entries:
        keyword = str.__str__(entry[0])
        if keyword == "key" and not has_key:
            key = entry[1]
            has_key = True
        elif keyword == second_name and not has_second:
            second = entry[1]
            has_second = True
        else:
            invalid = True
    if invalid:
        # CPython's error path uses rich comparison and str() for diagnostics.
        # Those hooks may raise, but cannot make an unrecognized name valid.
        for entry in entries:
            keyword = entry[0]
            if keyword not in ("key", second_name):
                diagnostic = str.__str__(str(keyword))
                raise TypeError(
                    name + "() got an unexpected keyword argument '" + diagnostic + "'"
                )
        raise TypeError("invalid keyword argument for " + name + "()")
    return (key, second, has_second)


def _clover_min_max(args, kwargs, want_max):
    name = "max" if want_max else "min"
    count = len(args)
    if count == 0:
        raise TypeError(name + " expected at least 1 argument, got 0")
    options = _clover_selection_keywords(kwargs, name, "default", None)
    key = options[0]
    default = options[1]
    has_default = options[2]
    if count > 1 and has_default:
        raise TypeError(
            "Cannot specify a default for " + name + "() with multiple positional arguments"
        )
    iterable = args[0] if count == 1 else args
    first = True
    for item in iterable:
        item_key = item if key is None else key(item)
        if first:
            best = item
            best_key = item_key
            first = False
        elif (item_key > best_key) if want_max else (item_key < best_key):
            best = item
            best_key = item_key
    if first:
        if has_default:
            return default
        raise ValueError(name + "() iterable argument is empty")
    return best


def _clover_list_init(self, iterable=(), /):
    list.clear(self)
    for item in iterable:
        list.append(self, item)


def _clover_list_sort_items(items, key, reverse):
    # This helper is entered once with an exact List holding detached storage.
    # Keep key calls, comparisons, truth conversion, and merge loops in Python.
    count = len(items)
    sort_keys = items
    if key is not None:
        sort_keys = []
        for item in items:
            sort_keys.append(key(item))
    if count < 2:
        return None

    order = []
    index = 0
    while index < count:
        order.append(index)
        index += 1
    if reverse:
        order.reverse()
    scratch = order.copy()
    width = 1
    while width < count:
        begin = 0
        while begin < count:
            middle = begin + width
            if middle > count:
                middle = count
            end = begin + 2 * width
            if end > count:
                end = count
            left = begin
            right = middle
            output = begin
            while left < middle and right < end:
                if sort_keys[order[right]] < sort_keys[order[left]]:
                    scratch[output] = order[right]
                    right += 1
                else:
                    scratch[output] = order[left]
                    left += 1
                output += 1
            while left < middle:
                scratch[output] = order[left]
                output += 1
                left += 1
            while right < end:
                scratch[output] = order[right]
                output += 1
                right += 1
            begin = end
        previous = order
        order = scratch
        scratch = previous
        width *= 2
    if reverse:
        order.reverse()

    # No Python callbacks run while applying the permutation to the exact List.
    # The original order is untouched until every comparison has succeeded.
    index = 0
    while index < count:
        if order[index] != index:
            saved = items[index]
            destination = index
            while order[destination] != index:
                source = order[destination]
                items[destination] = items[source]
                order[destination] = destination
                destination = source
            items[destination] = saved
            order[destination] = destination
        index += 1
    return None


def _clover_list_sort(self, /, *, key=None, reverse=False):
    """Sort the list in place, stably, and return None."""
    if not isinstance(self, list):
        raise TypeError("list.sort expects a list receiver")
    reverse = True if reverse else False
    return _clover_list_sort_impl(self, key, _clover_list_sort_items, reverse)


def _clover_list_eq(self, other, /):
    if not isinstance(self, list):
        raise TypeError("list.__eq__ expects a list receiver")
    if not isinstance(other, list):
        return NotImplemented
    if list.__len__(self) != list.__len__(other):
        return False
    i = 0
    while i < list.__len__(self) and i < list.__len__(other):
        a = list.__getitem__(self, i)
        b = list.__getitem__(other, i)
        if a is not b:
            if not (a == b):
                break
        i += 1
    # A callback can exhaust either list even when its comparison was false.
    if i >= list.__len__(self) or i >= list.__len__(other):
        return list.__len__(self) == list.__len__(other)
    return False


# Inherited __ne__ compares list payloads, not a subclass's __eq__ override.
def _clover_list_ne(self, other, /):
    if not isinstance(self, list):
        raise TypeError("list.__ne__ expects a list receiver")
    result = _clover_list_eq(self, other)
    if result is NotImplemented:
        return result
    return not result


def _clover_list_count(self, needle, /):
    """Return number of occurrences of value."""
    if not isinstance(self, list):
        raise TypeError
    count = 0
    i = 0
    while i < list.__len__(self):
        item = list.__getitem__(self, i)
        if item is needle or item == needle:
            count += 1
        i += 1
    return count


def _clover_list_index(self, needle, start=0, stop=sys.maxsize, /):
    if not isinstance(self, list):
        raise TypeError
    if (
        not isinstance(start, int)
        or not isinstance(stop, int)
    ):
        raise TypeError
    size = list.__len__(self)
    if start < 0:
        start += size
        if start < 0:
            start = 0
    if stop < 0:
        stop += size
        if stop < 0:
            stop = 0
    i = start
    while i < stop and i < list.__len__(self):
        item = list.__getitem__(self, i)
        if item is needle or item == needle:
            return int(i)
        i += 1
    raise ValueError


def _clover_tuple_eq(self, other, /):
    if not isinstance(self, tuple):
        raise TypeError("tuple.__eq__ expects a tuple receiver")
    if not isinstance(other, tuple):
        return NotImplemented
    left_size = tuple.__len__(self)
    right_size = tuple.__len__(other)
    i = 0
    while i < left_size and i < right_size:
        a = tuple.__getitem__(self, i)
        b = tuple.__getitem__(other, i)
        if a is not b:
            if not (a == b):
                return False
        i += 1
    return left_size == right_size


# Inherited __ne__ compares tuple payloads, not a subclass's __eq__ override.
def _clover_tuple_ne(self, other, /):
    if not isinstance(self, tuple):
        raise TypeError("tuple.__ne__ expects a tuple receiver")
    result = _clover_tuple_eq(self, other)
    if result is NotImplemented:
        return result
    return not result


# Slice fields are immutable; tuple comparison owns callback and truth semantics.
def _clover_slice_eq(self, other, /):
    if not isinstance(self, slice):
        raise TypeError("slice.__eq__ expects a slice receiver")
    if not isinstance(other, slice):
        return NotImplemented
    if self is other:
        return True
    return (self.start, self.stop, self.step) == (other.start, other.stop, other.step)


def _clover_slice_ne(self, other, /):
    if not isinstance(self, slice):
        raise TypeError("slice.__ne__ expects a slice receiver")
    result = _clover_slice_eq(self, other)
    if result is NotImplemented:
        return result
    return not result


def _clover_slice_lt(self, other, /):
    if not isinstance(self, slice):
        raise TypeError("slice.__lt__ expects a slice receiver")
    if not isinstance(other, slice):
        return NotImplemented
    if self is other:
        return False
    return (self.start, self.stop, self.step) < (other.start, other.stop, other.step)


def _clover_slice_le(self, other, /):
    if not isinstance(self, slice):
        raise TypeError("slice.__le__ expects a slice receiver")
    if not isinstance(other, slice):
        return NotImplemented
    if self is other:
        return True
    return (self.start, self.stop, self.step) <= (other.start, other.stop, other.step)


def _clover_slice_gt(self, other, /):
    if not isinstance(self, slice):
        raise TypeError("slice.__gt__ expects a slice receiver")
    if not isinstance(other, slice):
        return NotImplemented
    if self is other:
        return False
    return (self.start, self.stop, self.step) > (other.start, other.stop, other.step)


def _clover_slice_ge(self, other, /):
    if not isinstance(self, slice):
        raise TypeError("slice.__ge__ expects a slice receiver")
    if not isinstance(other, slice):
        return NotImplemented
    if self is other:
        return True
    return (self.start, self.stop, self.step) >= (other.start, other.stop, other.step)


def _clover_tuple_count(self, needle, /):
    """Return number of occurrences of value."""
    if not isinstance(self, tuple):
        raise TypeError
    size = tuple.__len__(self)
    count = 0
    i = 0
    while i < size:
        item = tuple.__getitem__(self, i)
        if item is needle or item == needle:
            count += 1
        i += 1
    return count


def _clover_tuple_index(self, needle, start=0, stop=sys.maxsize, /):
    if not isinstance(self, tuple):
        raise TypeError
    if (
        not isinstance(start, int)
        or not isinstance(stop, int)
    ):
        raise TypeError
    size = tuple.__len__(self)
    if start < 0:
        start += size
        if start < 0:
            start = 0
    if stop < 0:
        stop += size
        if stop < 0:
            stop = 0
    i = start
    while i < stop and i < size:
        item = tuple.__getitem__(self, i)
        if item is needle or item == needle:
            return int(i)
        i += 1
    raise ValueError


def _clover_list_remove(self, needle, /):
    """Remove first occurrence of value."""
    if not isinstance(self, list):
        raise TypeError
    i = 0
    while i < list.__len__(self):
        item = list.__getitem__(self, i)
        if item is needle or item == needle:
            # Equality can remove this slot. CPython still reports success.
            if i < list.__len__(self):
                list.__delitem__(self, i)
            return None
        i += 1
    raise ValueError


def _clover_iterable_to_exact_tuple(source):
    builder = []
    for item in source:
        __clover_list_append__(builder, item)
    return __clover_list_to_tuple__(builder)


def _clover_iterable_to_exact_list(source):
    builder = []
    for item in source:
        __clover_list_append__(builder, item)
    return builder


def _clover_merge_call_keyword_mapping(callable, destination, source):
    try:
        keys = source.keys()
    except AttributeError:
        _clover_raise_call_mapping_error(callable, source)
    keys = __clover_to_exact_list__(keys)
    for key in keys:
        if key in destination:
            _clover_raise_duplicate_keyword(callable, key)
        destination[key] = source[key]
    return None


def _clover_list_extend(builder, source):
    for item in source:
        __clover_list_append__(builder, item)
    return None


def _clover_enumerate_call(cls, /, *args, **kwargs):
    _clover_enumerate_validate_class(cls)
    count = len(args) + len(kwargs)
    # CPython's public enumerate call accepts both named parameters, but its
    # two-argument path requires a lone keyword to be "start".
    if count == 1 or count == 2:
        iterable = None
        start = 0
        if len(args) > 0:
            iterable = args[0]
        if len(args) == 2:
            start = args[1]
        has_iterable = len(args) > 0
        has_start = len(args) == 2
        for entry in kwargs.items():
            name = str.__str__(entry[0])
            if name == "iterable" and not has_iterable:
                iterable = entry[1]
                has_iterable = True
            elif name == "start" and count == 2 and not has_start:
                start = entry[1]
                has_start = True
            else:
                raise TypeError(
                    "'" + str(entry[0]) + "' is an invalid keyword argument for enumerate()"
                )
        if has_iterable:
            start = _clover_enumerate_normalize_start(start)
            source = iter(iterable)
            return _clover_enumerate_allocate(cls, source, start)
    if len(args) == 0:
        raise TypeError("enumerate() missing required argument 'iterable'")
    raise TypeError("enumerate() takes at most 2 arguments (" + str(count) + " given)")


_clover_enumerate_missing = object()


def _clover_enumerate_new(cls, /, *args, **kwargs):
    _clover_enumerate_validate_class(cls)
    positional = len(args)
    remaining = len(kwargs)
    if positional + remaining > 2:
        qualifier = "keyword " if positional == 0 else ""
        raise TypeError(
            "enumerate() takes at most 2 " + qualifier + "arguments ("
            + str(positional + remaining) + " given)"
        )
    # Match CPython's Argument Clinic __new__ entry: required keyword lookup
    # comes first, using the stored dictionary hashes and rich equality.
    if positional > 0:
        iterable = args[0]
    else:
        iterable = kwargs.get("iterable", _clover_enumerate_missing)
        if iterable is _clover_enumerate_missing:
            raise TypeError("enumerate() missing required argument 'iterable' (pos 1)")
        remaining -= 1
    start = 0
    if positional > 1:
        start = args[1]
    elif remaining > 0:
        candidate = kwargs.get("start", _clover_enumerate_missing)
        if candidate is not _clover_enumerate_missing:
            start = candidate
            remaining -= 1
    if remaining > 0:
        if positional > 0:
            duplicate = kwargs.get("iterable", _clover_enumerate_missing)
            if duplicate is not _clover_enumerate_missing:
                raise TypeError(
                    "argument for enumerate() given by name ('iterable') and position (1)"
                )
        for keyword in kwargs:
            if keyword not in ("iterable", "start"):
                diagnostic = str.__str__(str(keyword))
                raise TypeError(
                    "enumerate() got an unexpected keyword argument '" + diagnostic + "'"
                )
        raise TypeError("invalid keyword argument for enumerate()")
    start = _clover_enumerate_normalize_start(start)
    source = iter(iterable)
    return _clover_enumerate_allocate(cls, source, start)


def _clover_enumerate_next(self, /):
    source = _clover_enumerate_source(self)
    item = next(source)
    index = _clover_enumerate_take_index(self)
    return (index, item)


_clover_zip_missing = object()


def _clover_zip_new(cls, /, *iterables, **kwargs):
    _clover_zip_validate_class(cls)
    if len(kwargs) > 1:
        raise TypeError(
            "zip() takes at most 1 keyword argument (" + str(len(kwargs)) + " given)"
        )
    strict = kwargs.get("strict", _clover_zip_missing)
    if strict is _clover_zip_missing:
        for name in kwargs:
            if str.__str__(name) != "strict":
                raise TypeError("zip() got an unexpected keyword argument '" + str(name) + "'")
            raise TypeError("invalid keyword argument for zip()")
        strict = False
    else:
        strict = True if strict else False
    sources = []
    for iterable in iterables:
        __clover_list_append__(sources, iter(iterable))
    return _clover_zip_allocate(cls, __clover_list_to_tuple__(sources), strict)


def _clover_zip_next(self, /):
    sources = _clover_zip_sources(self)
    strict = _clover_zip_strict(self)
    count = len(sources)
    if count == 0:
        raise StopIteration
    row = []
    i = 0
    while i < count:
        try:
            item = next(sources[i])
        except StopIteration:
            if not strict:
                raise
            break
        __clover_list_append__(row, item)
        i += 1
    if i == count:
        return __clover_list_to_tuple__(row)

    # Leave the exhaustion handler before reporting a strict mismatch or
    # final exhaustion, so the discarded source exception is not chained.
    if i > 0:
        suffix = " 1" if i == 1 else "s 1-" + str(i)
        raise ValueError(
            "zip() argument " + str(i + 1) + " is shorter than argument" + suffix
        )
    # Input 1 ended: probe each remaining input once. A yielded item is
    # consumed before reporting a mismatch. Other errors propagate unchanged.
    j = 1
    while j < count:
        try:
            next(sources[j])
        except StopIteration:
            j += 1
        else:
            break
    if j < count:
        suffix = " 1" if j == 1 else "s 1-" + str(j)
        raise ValueError(
            "zip() argument " + str(j + 1) + " is longer than argument" + suffix
        )
    raise StopIteration


# bytearray uses the same new-only construction boundary as dict. Native
# storage operations never enter Python; callbacks live in these managed loops.
def _clover_bytearray_new(cls, /, source=()):
    result = _clover_bytearray_allocate(cls)
    if _clover_bytearray_initialize_native(result, source):
        return result
    for item in source:
        _clover_bytearray_append(result, item)
    return result


def _clover_bytearray_snapshot(source):
    result = _clover_bytearray_allocate(bytearray)
    if _clover_bytearray_copy_native(result, source):
        return result
    for item in source:
        _clover_bytearray_append(result, item)
    return result


def _clover_bytearray_extend(self, source, /):
    if _clover_bytearray_extend_native(self, source):
        return
    staged = _clover_bytearray_snapshot(source)
    _clover_bytearray_extend_native(self, staged)


# Shared by the generic raw loop and the FileIO wrapper. Raw readall observes
# each read override; FileIO readall calls only the retained native primitive.
_clover_rawio_chunk_size = 128 * 1024


def _clover_rawio_index(value):
    if isinstance(value, int):
        return value
    return __clover_call_special__(
        value, "__index__", TypeError, "object cannot be interpreted as an integer"
    )


def _clover_rawio_read(self, size=-1, /):
    _clover_rawio_validate(self)
    size = _clover_rawio_read_size(_clover_rawio_index(size))
    if size < 0:
        return self.readall()
    buffer = bytearray(size)
    count = self.readinto(buffer)
    if count is None:
        return None
    return _clover_rawio_finish_read(buffer, _clover_rawio_index(count), size)


def _clover_rawio_is_eintr(error):
    try:
        return _clover_rawio_errno_is_eintr(_clover_rawio_index(error.errno))
    except BaseException:
        # Match the reference trap: failed errno conversion must not replace
        # the original I/O error being considered for retry.
        return False


def _clover_rawio_readall(self, /):
    _clover_rawio_validate(self)
    result = bytearray()
    while True:
        try:
            chunk = self.read(_clover_rawio_chunk_size)
        except OSError as error:
            if _clover_rawio_is_eintr(error):
                continue
            raise
        if chunk is None:
            if bytearray.__len__(result) == 0:
                return None
            return bytes(result)
        if not isinstance(chunk, bytes):
            raise TypeError('read() should return bytes')
        if bytes.__len__(chunk) == 0:
            return bytes(result)
        bytearray.extend(result, chunk)


def _clover_fileio_readall(self, /):
    result = bytearray()
    while True:
        chunk = _clover_fileio_native_read(self, _clover_rawio_chunk_size)
        if chunk is None:
            if bytearray.__len__(result) == 0:
                return None
            return bytes(result)
        if bytes.__len__(chunk) == 0:
            return bytes(result)
        bytearray.extend(result, chunk)


# BufferedIOBase's transfer fallbacks contain no buffering policy.
def _clover_bufferedio_read(self, size=-1, /):
    _clover_bufferedio_validate(self)
    _clover_bufferedio_read_size(_clover_rawio_index(size))
    raise _clover_io_unsupported('read')


def _clover_bufferedio_read1(self, size=-1, /):
    _clover_bufferedio_validate(self)
    _clover_bufferedio_read_size(_clover_rawio_index(size))
    raise _clover_io_unsupported('read1')


def _clover_bufferedio_readinto(self, buffer, /):
    size = _clover_bufferedio_readinto_size(self, buffer)
    data = self.read(size)
    return _clover_bufferedio_finish_readinto(buffer, data, size)


def _clover_bufferedio_readinto1(self, buffer, /):
    size = _clover_bufferedio_readinto_size(self, buffer)
    data = self.read1(size)
    return _clover_bufferedio_finish_readinto(buffer, data, size)


class BufferedReader(BufferedIOBase):
    __module__ = '_io'

    def __init__(self, raw, buffer_size=_clover_bufferedio_default_buffer_size):
        if hasattr(self, '_busy') and self._busy:
            raise RuntimeError('reentrant call inside BufferedReader')
        self.raw = None
        self._busy = True
        self._pos = 0
        self._end = 0
        try:
            size = _clover_rawio_read_size(_clover_rawio_index(buffer_size))
            if not raw.readable():
                raise _clover_io_unsupported('File or stream is not readable')
            if size <= 0:
                raise ValueError('buffer size must be strictly positive')
            buffer = bytearray(size)
            self._buffer_size = size
            self._buffer = buffer
            self.raw = raw
            _clover_bufferedio_set_closed(self, False)
        finally:
            self._busy = False

    def _check_open(self):
        if self.raw is None:
            raise ValueError('raw stream has been detached')
        if self.closed:
            raise ValueError('I/O operation on closed file')

    def _begin(self):
        if self._busy:
            raise RuntimeError('reentrant call inside BufferedReader')
        self._check_open()
        self._busy = True
        return self.raw

    def _raw_open(self, raw):
        if raw.closed:
            raise ValueError('I/O operation on closed file')

    def _take(self, size):
        data = _clover_bufferedio_copy(self._buffer, self._pos, size)
        self._pos += size
        return data

    def _raw_into(self, raw, buffer, size):
        self._raw_open(raw)
        while True:
            try:
                count = raw.readinto(buffer)
            except OSError as error:
                if _clover_rawio_is_eintr(error):
                    continue
                raise
            if count is None:
                return None
            try:
                count = _clover_rawio_index(count)
            except BaseException:
                raise OSError('raw readinto() failed')
            return _clover_bufferedio_read_count(buffer, count, size)

    def _fill(self, raw):
        # No unread data remains. Replace a scratch buffer resized by a prior
        # raw callback before issuing another full-capacity request.
        self._pos = 0
        self._end = 0
        if bytearray.__len__(self._buffer) != self._buffer_size:
            self._buffer = bytearray(self._buffer_size)
        count = self._raw_into(raw, self._buffer, self._buffer_size)
        if count is not None:
            self._end = count
        return count

    def _read_sized(self, raw, size, one_raw):
        if size == 0:
            return b''
        available = self._end - self._pos
        if size <= available:
            return self._take(size)
        result = bytearray()
        if available:
            bytearray.extend(result, self._take(available))
        remaining = size - available
        calls = 0
        while remaining > 0:
            if one_raw and calls:
                break
            # readinto1 can combine buffered bytes with one large direct read,
            # but does not refill merely to top up an already partial result.
            if one_raw and bytearray.__len__(result) and remaining <= self._buffer_size:
                break
            if remaining >= self._buffer_size:
                wanted = remaining
                if not one_raw:
                    wanted = (remaining // self._buffer_size) * self._buffer_size
                buffer = bytearray(wanted)
                count = self._raw_into(raw, buffer, wanted)
                calls += 1
                if count is None:
                    if bytearray.__len__(result) == 0:
                        return None
                    break
                if count == 0:
                    break
                bytearray.extend(result, _clover_bufferedio_copy(buffer, 0, count))
                remaining -= count
            else:
                count = self._fill(raw)
                calls += 1
                if count is None:
                    if bytearray.__len__(result) == 0:
                        return None
                    break
                if count == 0:
                    break
                take = remaining
                if take > count:
                    take = count
                bytearray.extend(result, self._take(take))
                remaining -= take
        return bytes(result)

    def _read_all(self, raw):
        result = bytearray()
        available = self._end - self._pos
        if available:
            bytearray.extend(result, self._take(available))
        self._raw_open(raw)
        if hasattr(raw, 'readall'):
            chunk = raw.readall()
            if chunk is None:
                if bytearray.__len__(result) == 0:
                    return None
                return bytes(result)
            _clover_bufferedio_bytes_size(chunk)
            if bytearray.__len__(result) == 0:
                return chunk
            bytearray.extend(result, chunk)
            return bytes(result)
        while True:
            try:
                chunk = raw.read()
            except OSError as error:
                if _clover_rawio_is_eintr(error):
                    continue
                raise
            if chunk is None:
                if bytearray.__len__(result) == 0:
                    return None
                break
            if _clover_bufferedio_bytes_size(chunk) == 0:
                break
            bytearray.extend(result, chunk)
        return bytes(result)

    def read(self, size=-1, /):
        if size is None:
            size = -1
        size = _clover_rawio_read_size(_clover_rawio_index(size))
        raw = self._begin()
        try:
            if size < 0:
                return self._read_all(raw)
            return self._read_sized(raw, size, False)
        finally:
            self._busy = False

    def read1(self, size=-1, /):
        size = _clover_rawio_read_size(_clover_rawio_index(size))
        raw = self._begin()
        try:
            if size < 0:
                size = self._buffer_size
            available = self._end - self._pos
            if available:
                if size > available:
                    size = available
                return self._take(size)
            if size == 0:
                return b''
            buffer = bytearray(size)
            count = self._raw_into(raw, buffer, size)
            if count is None:
                return b''
            return _clover_bufferedio_copy(buffer, 0, count)
        finally:
            self._busy = False

    def peek(self, size=0, /):
        _clover_rawio_read_size(_clover_rawio_index(size))
        raw = self._begin()
        try:
            if self._pos == self._end:
                self._fill(raw)
            return _clover_bufferedio_copy(self._buffer, self._pos, self._end - self._pos)
        finally:
            self._busy = False

    def readinto(self, buffer, /):
        size = _clover_bufferedio_readinto_size(self, buffer)
        raw = self._begin()
        try:
            data = self._read_sized(raw, size, False)
            if data is None:
                return None
            return _clover_bufferedio_finish_readinto(buffer, data, size)
        finally:
            self._busy = False

    def readinto1(self, buffer, /):
        size = _clover_bufferedio_readinto_size(self, buffer)
        raw = self._begin()
        try:
            data = self._read_sized(raw, size, True)
            if data is None:
                return None
            return _clover_bufferedio_finish_readinto(buffer, data, size)
        finally:
            self._busy = False

    def tell(self):
        raw = self._begin()
        try:
            position = _clover_rawio_read_size(_clover_rawio_index(raw.tell()))
            if position < 0:
                raise OSError('tell() returned an invalid position')
            position -= self._end - self._pos
            if position < 0:
                return 0
            return position
        finally:
            self._busy = False

    def seek(self, offset, whence=0, /):
        offset = _clover_rawio_read_size(_clover_rawio_index(offset))
        whence = _clover_bufferedio_seek_whence(_clover_rawio_index(whence))
        raw = self._begin()
        try:
            self._raw_open(raw)
            if whence == 1:
                offset -= self._end - self._pos
            position = _clover_rawio_read_size(_clover_rawio_index(raw.seek(offset, whence)))
            if position < 0:
                raise OSError('seek() returned an invalid position')
            self._pos = 0
            self._end = 0
            return position
        finally:
            self._busy = False

    def readable(self):
        self._check_open()
        return self.raw.readable()

    def writable(self):
        return False

    def seekable(self):
        self._check_open()
        return self.raw.seekable()

    def fileno(self):
        self._check_open()
        return self.raw.fileno()

    def isatty(self):
        self._check_open()
        return self.raw.isatty()

    def flush(self):
        # Stateless delegation: IOBase.close invokes this while close owns the
        # operation guard. Reads/seeks remain protected during that callback.
        self._check_open()
        return self.raw.flush()

    def close(self):
        if self.raw is None:
            raise ValueError('raw stream has been detached')
        if self.closed:
            return None
        raw = self._begin()
        try:
            if raw.closed:
                _clover_bufferedio_set_closed(self, True)
                return None
            try:
                IOBase.close(self)
            finally:
                raw.close()
        finally:
            self._busy = False

    def detach(self):
        raw = self._begin()
        try:
            self.flush()
            self.raw = None
            self._pos = 0
            self._end = 0
            return raw
        finally:
            self._busy = False
