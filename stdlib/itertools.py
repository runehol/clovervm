"""Lazy iteration building blocks supported by CloverVM.

This initial subset provides chain, repeat, islice, and starmap. Integer
arguments use operator.index's builtin-int/bool support; custom __index__ is
not supported yet. Limits match the VM's current signed 64-bit platforms.
chain.from_iterable is callable on the class only: classmethod binding and
subclass-preserving construction are deferred. starmap uses the VM's existing
call support; callable instances are not supported.
Inputs use existing VM iterable types; string iteration is not supported yet.
"""

from operator import index as _index


__all__ = ("chain", "repeat", "islice", "starmap")

_missing = object()
_SSIZE_MAX = 2**63 - 1
_SSIZE_MIN = -(2**63)


class chain:
    def __init__(self, *iterables):
        self._source = iter(iterables)
        self._active = None

    # Deliberately a class-level function until classmethod is supported.
    def from_iterable(iterable, /):
        source = iter(iterable)
        result = chain()
        result._source = source
        return result

    def __iter__(self):
        return self

    def __next__(self):
        while self._source is not None:
            if self._active is None:
                try:
                    iterable = next(self._source)
                except StopIteration:
                    # Outer exhaustion discards StopIteration arguments.
                    self._source = None
                    break
                except:
                    self._source = None
                    raise
                try:
                    self._active = iter(iterable)
                except:
                    # Failure obtaining an inner iterator closes the chain.
                    self._source = None
                    raise
            try:
                return next(self._active)
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
        self._iterator = iter(iterable)
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
                next(iterator)
                self._position += 1
            if self._stop is not None and self._position >= self._stop:
                raise StopIteration
            value = next(iterator)
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
        self._iterator = iter(iterable)

    def __iter__(self):
        return self

    def __next__(self):
        args = next(self._iterator)
        function = self._function
        return function(*args)
