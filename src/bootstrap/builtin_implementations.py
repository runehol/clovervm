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
    return __clover_call_special__(
        obj, "__len__", TypeError, "object has no len()"
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
