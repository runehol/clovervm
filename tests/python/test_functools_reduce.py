# Python test set -- functools module
#
# Portions adapted from CPython's Lib/test/test_functools.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Additional streaming, identity, and reentry probes checked on CPython 3.14.5.

import functools
import sys
from functools import reduce


def raises(error, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


def add(a, b):
    return a + b


assert sys.modules["functools"] is functools
assert functools.__name__ == "functools"
assert functools.__package__ == ""
assert functools.__file__.endswith("functools.py")
assert reduce(add, [1, 2, 3]) == 6
assert reduce(add, (1, 2, 3), 10) == 16
assert reduce(add, [1, 2, 3], initial=10) == 16
assert reduce(add, ["a", "b", "c"], "") == "abc"
assert reduce(add, [[1], [], [2, 3]], []) == [1, 2, 3]
assert reduce(add, [], None) is None
assert reduce(add, [], initial=None) is None

marker = []
assert reduce(None, [], marker) is marker
assert reduce(None, [marker]) is marker
assert str(raises(TypeError, reduce, None, [])) == (
    "reduce() of empty iterable with no initial value"
)
raises(TypeError, reduce, None, [1, 2])
raises(TypeError, reduce, None, [1], None)
raises(TypeError, reduce)
raises(TypeError, reduce, add)
raises(TypeError, reduce, add, [], 0, 1)
raises(TypeError, reduce, add, [], 0, initial=1)
raises(TypeError, reduce, function=add, iterable=[])
raises(TypeError, reduce, add, iterable=[])
raises(TypeError, reduce, add, [], extra=1)


def with_none(a, b):
    assert a is None
    return b


assert reduce(with_none, [marker], initial=None) is marker


class Traced:
    def __init__(self, values, events):
        self.values = values
        self.events = events
        self.position = 0

    def __iter__(self):
        self.events.append("iter")
        return self

    def __next__(self):
        self.events.append("next")
        if self.position == len(self.values):
            raise StopIteration("done")
        value = self.values[self.position]
        self.position += 1
        return value

    def __len__(self):
        raise AssertionError("length queried")

    def __length_hint__(self):
        raise AssertionError("length hint queried")


events = []
source = Traced([1, 2, 3], events)


def subtract(a, b):
    events.append((a, b))
    return a - b


assert reduce(subtract, source) == -4
assert events == ["iter", "next", "next", (1, 2), "next", (-1, 3), "next"]
events = []
assert reduce(subtract, Traced([1, 2], events), 10) == 7
assert events == ["iter", "next", (10, 1), "next", (9, 2), "next"]


events = []
source = Traced([1, 2, 3], events)
raises(TypeError, reduce, None, source)
assert source.position == 2
assert events == ["iter", "next", "next"]
events = []
source = Traced([1, 2, 3], events)
raises(TypeError, reduce, None, source, initial=None)
assert source.position == 1
assert events == ["iter", "next"]
events = []
source = Traced([1, 2, 3], events)
raises(TypeError, reduce, add, source, 0, initial=1)
assert events == []


class BadIterable:
    def __iter__(self):
        raise acquisition_error


acquisition_error = TypeError("original acquisition error")
caught = raises(TypeError, reduce, None, BadIterable())
assert caught is not acquisition_error
assert str(caught) == "reduce() arg 2 must support iteration"
# CloverVM omits an absent exception context attribute rather than exposing
# None. Still reject any context accidentally retained by the replacement.
assert getattr(caught, "__context__", None) is None
for acquisition_error in (ValueError("acquire"), StopIteration("acquire")):
    assert raises(acquisition_error.__class__, reduce, None, BadIterable()) is acquisition_error


class FailingIterator:
    def __init__(self):
        self.calls = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.calls += 1
        if self.calls == 1:
            return marker
        raise iteration_error


for iteration_error in (TypeError("advance"), ValueError("advance")):
    assert raises(iteration_error.__class__, reduce, add, FailingIterator()) is iteration_error
iteration_error = StopIteration("exhausted")
assert reduce(None, FailingIterator()) is marker


class End(StopIteration):
    pass


iteration_error = End("subclass exhausted")
assert reduce(None, FailingIterator()) is marker
for callback_error in (StopIteration("callback"), End("callback"), ValueError("callback")):
    events = []
    source = Traced([1, 2, 3], events)

    def fail(a, b):
        raise callback_error

    assert raises(callback_error.__class__, reduce, fail, source) is callback_error
    assert source.position == 2
    assert events == ["iter", "next", "next"]


def append(acc, value):
    acc.append(value)
    return acc


acc = []
assert reduce(append, [marker, None], acc) is acc
assert acc[0] is marker
assert acc[1] is None


def nested(a, b):
    return a + reduce(add, [b, 1])


assert reduce(nested, [1, 2, 3]) == 8


events = []
source = Traced([1, 2, 3, 4], events)


def consume(a, b):
    return a + b + next(source)


assert reduce(consume, source, 0) == 10
assert events == ["iter", "next", "next", "next", "next", "next"]


class Pair:
    def __init__(self, left, right):
        self.left = left
        self.right = right


pair = reduce(Pair, [marker, None, 3])
assert pair.left.left is marker
assert pair.left.right is None
assert pair.right == 3
# pow is a bootstrap function that calls a native arithmetic primitive.
assert reduce(pow, [2, 3, 2]) == 64
# Native fixed-arity callable dispatch.
assert reduce(isinstance, [marker, list]) is True

# Reuse the same callback call site across different targets and constructors.
for attempt in range(40):
    assert reduce(add, [1, 2, 3]) == 6
    assert reduce(pow, [2, 3, 2]) == 64
    assert reduce(Pair, [1, 2]).right == 2
