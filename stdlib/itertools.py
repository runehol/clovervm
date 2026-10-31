"""Lazy iteration building blocks supported by CloverVM.

The supported subset provides chain, repeat, islice, starmap, compress,
filterfalse, takewhile, dropwhile, pairwise, cycle, count, accumulate, batched,
zip_longest, product, combinations, combinations_with_replacement, and permutations.
Integer bounds use operator.index's builtin-int/bool support; custom __index__
is not supported yet. Limits match the VM's current signed 64-bit platforms.
count accepts builtin ints/bools/floats and their subclasses, not arbitrary numeric objects.
Floating steps use repeated addition, including its rounding error.
chain.from_iterable is callable on the class only: classmethod binding and
subclass-preserving construction are deferred. starmap uses the VM's existing
call support, as do predicate iterators; callable instances and first-class
bound methods are not supported. Inputs use existing VM iterable types.
Repr, pickling, copy, and CPython's tuple recycling are outside this subset.
cycle caches yielded objects lazily by reference and releases the source on
exhaustion. Infinite inputs grow the cache without bound.
batched builds batches incrementally rather than preallocating n tuple slots;
CPython's eager allocation failures and their timing are not reproduced.
zip_longest keeps final exhaustion sticky across input reentry, rather than
reproducing CPython 3.14.5's unsafe reentrant exhaustion and counter underflow.
product eagerly snapshots input pools, retains them after exhaustion, and shares
those snapshots across repeated dimensions. It does not cache result tuples or
reproduce CPython's allocation-failure timing.
The combinatorial iterators also retain snapshots after exhaustion and build
independent result tuples. They check the reference's index-array size bound,
but avoid allocating index state for results already known to be empty.
"""

from builtins import bool as _bool, float as _float, int as _int
from builtins import isinstance as _isinstance, iter as _iter, next as _next
from builtins import tuple as _tuple
from operator import index as _index


__all__ = (
    "chain", "repeat", "islice", "starmap",
    "compress", "filterfalse", "takewhile", "dropwhile",
    "pairwise", "cycle", "count", "accumulate", "batched", "zip_longest", "product",
    "combinations", "combinations_with_replacement", "permutations",
)

_missing = object()
_SSIZE_MAX = 2**63 - 1
_SSIZE_MIN = -(2**63)


def _combinatorial_args(args, options, name, default=_missing):
    # Variadic initializers let explicit reinitialization bypass all binding,
    # just like the native types whose construction lives in __new__.
    nargs = len(args)
    remaining = len(options)
    if nargs + remaining > 2:
        raise TypeError(name + "() expects at most 2 arguments")
    # A local sentinel distinguishes even explicit module-private objects from
    # absence. Each canonical-name lookup runs once, in Argument Clinic order;
    # iterating/comparing keyword keys would add subclass comparison callbacks.
    absent = object()
    if nargs:
        iterable = args[0]
    else:
        iterable = options.get("iterable", absent)
        if iterable is absent:
            raise TypeError(name + "() missing required argument iterable")
        remaining -= 1
    if nargs == 2:
        r = args[1]
    else:
        r = absent
        if remaining:
            r = options.get("r", absent)
        if r is not absent:
            remaining -= 1
        elif default is not _missing:
            r = default
        else:
            raise TypeError(name + "() missing required argument r")
    if remaining:
        if nargs and options.get("iterable", absent) is not absent:
            raise TypeError(name + "() got multiple values for iterable")
        raise TypeError(name + "() got an unexpected keyword argument")
    return iterable, r


def _combinatorial_index(r):
    r = _index(r)
    if r < _SSIZE_MIN or r > _SSIZE_MAX:
        raise OverflowError("Python int too large to convert to C ssize_t")
    return r


def _combinatorial_size(r):
    # CPython snapshots the pool before these checks, even for empty results.
    if r < 0:
        raise ValueError("r must be non-negative")
    if r > _SSIZE_MAX // 8:
        raise MemoryError


class combinations:
    def __init__(self, /, *args, **options):
        if hasattr(self, "_pool"):
            return
        arguments = _combinatorial_args(args, options, "combinations")
        iterable = arguments[0]
        r = _combinatorial_index(arguments[1])
        arguments = None
        pool = _tuple(iterable)
        _combinatorial_size(r)
        stopped = r > len(pool)
        indices = []
        if not stopped:
            indices = list(range(r))
        self._indices = indices
        self._r = r
        self._started = False
        self._stopped = stopped
        # Publish the completion marker only after every input callback.
        self._pool = pool

    def __iter__(self):
        return self

    def __next__(self):
        if self._stopped:
            raise StopIteration
        pool = self._pool
        indices = self._indices
        r = self._r
        try:
            if self._started:
                i = r - 1
                while i >= 0 and indices[i] == i + len(pool) - r:
                    i -= 1
                if i < 0:
                    raise StopIteration
                indices[i] += 1
                j = i + 1
                while j < r:
                    indices[j] = indices[j - 1] + 1
                    j += 1
            row = []
            for i in range(r):
                row.append(pool[indices[i]])
            result = _tuple(row)
        except:
            self._stopped = True
            raise
        self._started = True
        return result


class combinations_with_replacement:
    def __init__(self, /, *args, **options):
        if hasattr(self, "_pool"):
            return
        arguments = _combinatorial_args(
            args, options, "combinations_with_replacement")
        iterable = arguments[0]
        r = _combinatorial_index(arguments[1])
        arguments = None
        pool = _tuple(iterable)
        _combinatorial_size(r)
        stopped = len(pool) == 0 and r != 0
        indices = []
        if not stopped:
            for i in range(r):
                indices.append(0)
        self._indices = indices
        self._r = r
        self._started = False
        self._stopped = stopped
        self._pool = pool

    def __iter__(self):
        return self

    def __next__(self):
        if self._stopped:
            raise StopIteration
        pool = self._pool
        indices = self._indices
        r = self._r
        try:
            if self._started:
                i = r - 1
                while i >= 0 and indices[i] == len(pool) - 1:
                    i -= 1
                if i < 0:
                    raise StopIteration
                following = indices[i] + 1
                while i < r:
                    indices[i] = following
                    i += 1
            row = []
            for i in range(r):
                row.append(pool[indices[i]])
            result = _tuple(row)
        except:
            self._stopped = True
            raise
        self._started = True
        return result


class permutations:
    def __init__(self, /, *args, **options):
        if hasattr(self, "_pool"):
            return
        arguments = _combinatorial_args(args, options, "permutations", None)
        iterable = arguments[0]
        r = arguments[1]
        arguments = None
        pool = _tuple(iterable)
        n = len(pool)
        if r is None:
            r = n
        else:
            # Unlike combinations, CPython rejects non-int __index__ objects.
            if not _isinstance(r, _int):
                raise TypeError("Expected int as r")
            r = _combinatorial_index(r)
        _combinatorial_size(r)
        stopped = r > n
        indices = []
        cycles = []
        if not stopped:
            indices = list(range(n))
            for i in range(r):
                cycles.append(n - i)
        self._indices = indices
        self._cycles = cycles
        self._r = r
        self._started = False
        self._stopped = stopped
        self._pool = pool

    def __iter__(self):
        return self

    def __next__(self):
        if self._stopped:
            raise StopIteration
        pool = self._pool
        indices = self._indices
        cycles = self._cycles
        n = len(pool)
        r = self._r
        try:
            if self._started:
                i = r - 1
                while i >= 0:
                    cycles[i] -= 1
                    if cycles[i] == 0:
                        saved = indices[i]
                        j = i
                        while j < n - 1:
                            indices[j] = indices[j + 1]
                            j += 1
                        indices[n - 1] = saved
                        cycles[i] = n - i
                        i -= 1
                    else:
                        j = n - cycles[i]
                        saved = indices[i]
                        indices[i] = indices[j]
                        indices[j] = saved
                        break
                if i < 0:
                    raise StopIteration
            row = []
            for i in range(r):
                row.append(pool[indices[i]])
            result = _tuple(row)
        except:
            self._stopped = True
            raise
        self._started = True
        return result


class product:
    def __init__(self, /, *iterables, **options):
        # CPython constructs in __new__; subsequent __init__ calls are no-ops,
        # including calls with invalid arguments. Parse keywords only once.
        if hasattr(self, "_pools"):
            return
        if len(options) > 1 or (options and "repeat" not in options):
            raise TypeError("product() accepts only the repeat keyword")
        repeat = 1
        if options:
            repeat = _index(options["repeat"])
        if repeat < _SSIZE_MIN or repeat > _SSIZE_MAX:
            raise OverflowError("Python int too large to convert to C ssize_t")
        if repeat < 0:
            raise ValueError("repeat argument cannot be negative")
        count = len(iterables)
        # Match the 64-bit reference's checked index-array size before inputs.
        if repeat and count > _SSIZE_MAX // 8 // repeat:
            raise OverflowError("repeat argument too large")
        pools = []
        if repeat:
            for iterable in iterables:
                # Even an empty pool does not skip acquisition of later pools.
                pools.append(_tuple(iterable))
        original_count = len(pools)
        size = original_count * repeat
        index = original_count
        while index < size:
            pools.append(pools[index - original_count])
            index += 1
        indices = []
        stopped = False
        for pool in pools:
            indices.append(0)
            if not pool:
                stopped = True
        # Publish only complete state, after all input callbacks have finished.
        self._indices = indices
        self._started = False
        self._stopped = stopped
        self._pools = _tuple(pools)

    def __iter__(self):
        return self

    def __next__(self):
        if self._stopped:
            raise StopIteration
        pools = self._pools
        indices = self._indices
        try:
            if self._started:
                index = len(pools) - 1
                while index >= 0:
                    following = indices[index] + 1
                    if following < len(pools[index]):
                        indices[index] = following
                        break
                    indices[index] = 0
                    index -= 1
                if index < 0:
                    self._stopped = True
                    raise StopIteration
            row = []
            index = 0
            while index < len(pools):
                row.append(pools[index][indices[index]])
                index += 1
            result = _tuple(row)
        except:
            self._stopped = True
            raise
        self._started = True
        return result


class chain:
    def __init__(self, *iterables):
        self._source = _iter(iterables)
        self._active = None

    # Deliberately a class-level function until classmethod is supported.
    def from_iterable(iterable, /):
        source = _iter(iterable)
        result = chain()
        result._source = source
        return result

    def __iter__(self):
        return self

    def __next__(self):
        while self._source is not None:
            if self._active is None:
                try:
                    iterable = _next(self._source)
                except StopIteration:
                    # Outer exhaustion discards StopIteration arguments.
                    self._source = None
                    break
                except:
                    self._source = None
                    raise
                try:
                    self._active = _iter(iterable)
                except:
                    # Failure obtaining an inner iterator closes the chain.
                    self._source = None
                    raise
            try:
                return _next(self._active)
            except StopIteration:
                self._active = None
        raise StopIteration


class cycle:
    def __init__(self, /, *args, **options):
        # Construction lives in CPython's __new__; reinitialization is a no-op.
        if hasattr(self, "_saved"):
            return
        if options:
            raise TypeError("cycle() takes no keyword arguments")
        if len(args) != 1:
            raise TypeError("cycle() expects exactly one argument")
        source = _iter(args[0])
        self._iterator = source
        self._index = -1
        # Publish complete state after acquiring the source.
        self._saved = []

    def __iter__(self):
        return self

    def __next__(self):
        index = self._index
        if index < 0:
            # Retain the source across callbacks that can reenter and clear it.
            source = self._iterator
            try:
                item = _next(source)
            except StopIteration:
                # CPython 3.14.10 resets replay even after nested exhaustion.
                # Exhaustion is permanent even for a resumable source.
                index = 0
                self._index = 0
                self._iterator = None
            else:
                # Nested exhaustion does not cancel this successful outer call.
                self._saved.append(item)
                return item
        saved = self._saved
        if not saved:
            raise StopIteration
        item = saved[index]
        index += 1
        if index >= len(saved):
            index = 0
        self._index = index
        return item


class repeat:
    def __init__(self, object, times=_missing):
        if times is _missing:
            remaining = None
        else:
            remaining = _index(times)
            if remaining < _SSIZE_MIN or remaining > _SSIZE_MAX:
                raise OverflowError("Python int too large to convert to C ssize_t")
            if remaining < 0:
                remaining = 0
        self._object = object
        self._remaining = remaining

    def __iter__(self):
        return self

    def __next__(self):
        if self._remaining is not None:
            if self._remaining == 0:
                raise StopIteration
            self._remaining -= 1
        return self._object


def _slice_index(value, default, minimum, message):
    if value is None:
        return default
    try:
        result = _index(value)
    except TypeError:
        raise ValueError(message)
    if result < minimum or result > _SSIZE_MAX:
        raise ValueError(message)
    return result


class islice:
    def __init__(self, iterable, /, *bounds):
        size = len(bounds)
        if size < 1 or size > 3:
            raise TypeError("islice() expects 2 to 4 arguments")
        start = None
        step = None
        if size == 1:
            stop = bounds[0]
        else:
            start = bounds[0]
            stop = bounds[1]
            if size == 3:
                step = bounds[2]
        # CPython checks stop before reporting an invalid start.
        stop = _slice_index(
            stop, None, 0,
            "Stop argument for islice() must be None or an integer: 0 <= x <= sys.maxsize.",
        )
        start = _slice_index(
            start, 0, 0,
            "Indices for islice() must be None or an integer: 0 <= x <= sys.maxsize.",
        )
        step = _slice_index(
            step, 1, 1, "Step for islice() must be a positive integer or None.",
        )
        self._iterator = _iter(iterable)
        self._position = 0
        self._next_index = start
        self._stop = stop
        self._step = step

    def __iter__(self):
        return self

    def __next__(self):
        iterator = self._iterator
        if iterator is None:
            raise StopIteration
        try:
            while self._position < self._next_index:
                # Input callbacks can reenter and clear self._iterator.
                _next(iterator)
                self._position += 1
            if self._stop is not None and self._position >= self._stop:
                raise StopIteration
            value = _next(iterator)
        except:
            # Unlike chain's active iterator, islice closes on any input error.
            self._iterator = None
            raise
        self._position += 1
        next_index = self._next_index + self._step
        if next_index > _SSIZE_MAX:
            # Match CPython's overflow clamp, including its unbounded sentinel.
            next_index = self._stop
            if next_index is None:
                next_index = -1
        elif self._stop is not None and next_index > self._stop:
            next_index = self._stop
        self._next_index = next_index
        return value


class starmap:
    def __init__(self, function, iterable, /):
        self._function = function
        self._iterator = _iter(iterable)

    def __iter__(self):
        return self

    def __next__(self):
        args = _next(self._iterator)
        function = self._function
        return function(*args)


class compress:
    def __init__(self, data, selectors):
        self._data = _iter(data)
        self._selectors = _iter(selectors)

    def __iter__(self):
        return self

    def __next__(self):
        data = self._data
        selectors = self._selectors
        while True:
            # Fetch data first, even when selectors is exhausted or raises.
            datum = _next(data)
            selector = _next(selectors)
            if selector:
                return datum


class filterfalse:
    def __init__(self, function, iterable, /):
        self._function = function
        self._iterator = _iter(iterable)

    def __iter__(self):
        return self

    def __next__(self):
        iterator = self._iterator
        while True:
            item = _next(iterator)
            function = self._function
            if function is None or function is _bool:
                selected = item
            else:
                selected = function(item)
            if not selected:
                return item


class takewhile:
    def __init__(self, predicate, iterable, /):
        self._predicate = predicate
        self._iterator = _iter(iterable)
        self._stopped = False

    def __iter__(self):
        return self

    def __next__(self):
        # A nested call can stop the iterator without cancelling this call.
        if self._stopped:
            raise StopIteration
        item = _next(self._iterator)
        predicate = self._predicate
        if predicate(item):
            return item
        self._stopped = True
        raise StopIteration


class dropwhile:
    def __init__(self, predicate, iterable, /):
        self._predicate = predicate
        self._iterator = _iter(iterable)
        self._started = False

    def __iter__(self):
        return self

    def __next__(self):
        iterator = self._iterator
        while True:
            item = _next(iterator)
            # Fetching an item can reenter and finish the dropping phase.
            if self._started:
                return item
            predicate = self._predicate
            if not predicate(item):
                self._started = True
                return item


class pairwise:
    def __init__(self, iterable, /):
        self._iterator = _iter(iterable)
        self._previous = None
        self._has_previous = False

    def __iter__(self):
        return self

    def __next__(self):
        iterator = self._iterator
        if iterator is None:
            raise StopIteration
        previous = self._previous
        # Every Python object, including this module's sentinels, is valid data.
        if not self._has_previous:
            try:
                previous = _next(iterator)
            except:
                self._iterator = None
                self._previous = None
                self._has_previous = False
                raise
            self._previous = previous
            self._has_previous = True
            # Initial input advancement can reenter and close this iterator.
            iterator = self._iterator
            if iterator is None:
                self._previous = None
                self._has_previous = False
                raise StopIteration
        try:
            current = _next(iterator)
        except:
            self._iterator = None
            self._previous = None
            self._has_previous = False
            raise
        # Keep the outer call's previous item across nested advancement.
        self._previous = current
        self._has_previous = True
        return (previous, current)


class count:
    def __init__(self, start=0, step=1):
        if not _isinstance(start, _int) and not _isinstance(start, _float):
            raise TypeError("a number is required")
        if not _isinstance(step, _int) and not _isinstance(step, _float):
            raise TypeError("a number is required")
        self._value = start
        self._step = step
        self._fast_integer = False
        if _isinstance(start, _int) and _isinstance(step, _int):
            # CPython's unit-step path reads integer payloads, bypassing
            # subclass conversion and arithmetic overrides until SSIZE_MAX.
            value = _int.__index__(start)
            stride = _int.__index__(step)
            if value >= _SSIZE_MIN and value <= _SSIZE_MAX and stride == 1:
                self._value = value
                self._fast_integer = True

    def __iter__(self):
        return self

    def __next__(self):
        value = self._value
        if self._fast_integer and value < _SSIZE_MAX:
            self._value = value + 1
            return value
        # Add before returning, and leave state unchanged if addition raises.
        # In particular, do not replace floating repeated addition by a
        # start + index * step formula, which has different rounding.
        following = value + self._step
        self._value = following
        self._fast_integer = False
        return value


class accumulate:
    def __init__(self, iterable, func=None, *, initial=None):
        self._iterator = _iter(iterable)
        self._function = func
        self._initial = initial
        self._total = None
        self._has_total = False

    def __iter__(self):
        return self

    def __next__(self):
        if self._initial is not None:
            self._total = self._initial
            self._initial = None
            self._has_total = True
            return self._total
        item = _next(self._iterator)
        # Input advancement may reenter and establish or update the total.
        if not self._has_total:
            self._total = item
            self._has_total = True
            return item
        total = self._total
        function = self._function
        if function is None:
            following = total + item
        else:
            following = function(total, item)
        # A failed operation consumes item but does not overwrite the total,
        # including any update made by a nested call. None is valid data.
        self._total = following
        return following


class batched:
    def __init__(self, iterable, n, *, strict=False):
        size = _index(n)
        if size < _SSIZE_MIN or size > _SSIZE_MAX:
            raise OverflowError("Python int too large to convert to C ssize_t")
        # Argument Clinic converts strict before validating n's positivity.
        strict = _bool(strict)
        if size < 1:
            raise ValueError("n must be at least one")
        self._iterator = _iter(iterable)
        self._size = size
        self._strict = strict

    def __iter__(self):
        return self

    def __next__(self):
        iterator = self._iterator
        if iterator is None:
            raise StopIteration
        batch = []
        # range's VM bounds are narrower than the signed platform-size limit.
        remaining = self._size
        while remaining > 0:
            try:
                item = _next(iterator)
            except StopIteration:
                if not batch:
                    self._iterator = None
                    raise StopIteration
                if self._strict:
                    self._iterator = None
                    raise ValueError("batched(): incomplete batch")
                # A short non-strict batch does not close a resumable source.
                break
            except:
                self._iterator = None
                raise
            batch.append(item)
            remaining -= 1
        # Keep the local source alive across reentrant closure of this object.
        return _tuple(batch)


class zip_longest:
    def __init__(self, *iterables, fillvalue=None):
        iterators = []
        for iterable in iterables:
            iterators.append(_iter(iterable))
        self._iterators = iterators
        self._active = len(iterators)
        self._fillvalue = fillvalue

    def __iter__(self):
        return self

    def __next__(self):
        if self._active == 0:
            raise StopIteration
        iterators = self._iterators
        row = []
        index = 0
        while index < len(iterators):
            iterator = iterators[index]
            if iterator is None:
                item = self._fillvalue
            else:
                try:
                    item = _next(iterator)
                except StopIteration:
                    # Nested advancement may already have closed this object.
                    # Keep exhaustion sticky instead of underflowing the count.
                    if self._active == 0:
                        raise StopIteration
                    self._active -= 1
                    if self._active == 0:
                        # Like CPython, retain the final source on closure.
                        raise StopIteration
                    iterators[index] = None
                    item = self._fillvalue
                except:
                    # Earlier items in this row are consumed, not retried.
                    self._active = 0
                    raise
            row.append(item)
            index += 1
        # Local references retain sources and partial rows across reentry.
        # An outer call may finish its row after a nested call closes self.
        return _tuple(row)
