"""Lazy iteration building blocks supported by CloverVM.

The supported subset provides chain, repeat, islice, starmap, compress,
filterfalse, takewhile, dropwhile, pairwise, count, accumulate, batched, and zip_longest.
Integer bounds use operator.index's builtin-int/bool support; custom __index__
is not supported yet. Limits match the VM's current signed 64-bit platforms.
count accepts builtin ints/bools/floats and their subclasses, not arbitrary numeric objects.
Floating steps use repeated addition, including its rounding error.
chain.from_iterable is callable on the class only: classmethod binding and
subclass-preserving construction are deferred. starmap uses the VM's existing
call support, as do predicate iterators; callable instances and first-class
bound methods are not supported. Inputs use existing VM iterable types.
Repr, pickling, copy, and CPython's tuple recycling are outside this subset.
batched builds batches incrementally rather than preallocating n tuple slots;
CPython's eager allocation failures and their timing are not reproduced.
zip_longest keeps final exhaustion sticky across input reentry, rather than
reproducing CPython 3.14.5's unsafe reentrant exhaustion and counter underflow.
"""

from builtins import bool as _bool, float as _float, int as _int
from builtins import isinstance as _isinstance, iter as _iter, next as _next
from builtins import tuple as _tuple
from operator import index as _index


__all__ = (
    "chain", "repeat", "islice", "starmap",
    "compress", "filterfalse", "takewhile", "dropwhile",
    "pairwise", "count", "accumulate", "batched", "zip_longest",
)

_missing = object()
_SSIZE_MAX = 2**63 - 1
_SSIZE_MIN = -(2**63)


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
