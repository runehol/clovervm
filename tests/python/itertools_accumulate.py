# Python test set -- itertools accumulate
#
# Portions adapted from CPython's Lib/test/test_itertools.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import itertools
from itertools import accumulate


def raises(kind, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as caught:
        return caught
    raise AssertionError("expected exception")


def multiply(a, b):
    return a * b


def pair(a, b):
    return (a, b)


def right(a, b):
    return b


class Source:
    def __init__(self, values, events):
        self.values = iter(values)
        self.events = events

    def __iter__(self):
        self.events.append("iter")
        return self

    def __next__(self):
        self.events.append("next")
        value = next(self.values)
        if isinstance(value, BaseException):
            raise value
        return value


def test_values_and_signatures():
    assert list(accumulate(range(6))) == [0, 1, 3, 6, 10, 15]
    assert list(accumulate(iterable=[1, 2, 3], func=None)) == [1, 3, 6]
    assert list(accumulate("abc")) == ["a", "ab", "abc"]
    assert list(accumulate([[1], [2], [3]])) == [[1], [1, 2], [1, 2, 3]]
    assert list(accumulate([2, 3, 4], multiply)) == [2, 6, 24]
    assert list(accumulate([1.5, 2.0, 3.0])) == [1.5, 3.5, 6.5]
    assert list(accumulate([True, True, False])) == [True, 2, 2]
    assert list(accumulate([2**100, 2**100])) == [2**100, 2**101]
    assert list(accumulate([], initial=None)) == []
    assert list(accumulate([], initial=0)) == [0]
    assert list(accumulate([10, 5, 1], initial=None)) == [10, 15, 16]
    assert list(accumulate([10, 5, 1], initial=100)) == [100, 110, 115, 116]
    assert list(accumulate([1, 2], pair, initial=False)) == [False, (False, 1), ((False, 1), 2)]
    assert list(accumulate([1, 2], func=pair)) == [1, (1, 2)]
    for marker in (None, False, (), [], object()):
        values = list(accumulate([marker, 2, marker], right))
        assert values[0] is marker
        assert values[1] == 2
        assert values[2] is marker
    if hasattr(itertools, "_missing"):
        # Publicly reachable helper objects must not collide with total state.
        marker = itertools._missing
        assert list(accumulate([marker, 2], pair)) == [marker, (marker, 2)]
    assert list(accumulate([None, 2, 3], pair)) == [None, (None, 2), ((None, 2), 3)]
    raises(TypeError, accumulate)
    raises(TypeError, accumulate, [], None, 0)
    raises(TypeError, accumulate, [], iterable=[])
    raises(TypeError, accumulate, [], unexpected=1)
    raises(TypeError, accumulate, 42)
    raises(TypeError, accumulate, 42, initial=10)
    assert list(accumulate([], 42)) == []
    assert list(accumulate([7], 42)) == [7]
    assert list(accumulate([], 42, initial=0)) == [0]
    selected = accumulate([1, 2, 3], 42)
    assert next(selected) == 1
    raises(TypeError, next, selected)
    raises(TypeError, next, selected)
    assert next(selected, "done") == "done"


def test_laziness_and_source_recovery():
    events = []
    selected = accumulate(Source([1, 2], events), initial=0)
    assert iter(selected) is selected
    assert events == ["iter"]
    assert next(selected) == 0
    assert events == ["iter"]
    assert next(selected) == 1
    assert events == ["iter", "next"]
    assert next(selected) == 3
    assert next(selected, "done") == "done"
    before = len(events)
    assert next(selected, "done") == "done"
    assert len(events) == before + 1  # Source is retained and probed again.
    for failure in (ValueError("source"), StopIteration("recoverable stop")):
        events = []
        selected = accumulate(Source([failure, 1, failure, 2], events))
        assert raises(failure.__class__, next, selected) is failure
        assert next(selected) == 1
        assert raises(failure.__class__, next, selected) is failure
        assert next(selected) == 3
        assert next(selected, "done") == "done"
        assert len(events) == 6


callback_events = []
callback_error = None
reentrant_target = None
reentrant_events = []


def failing_combine(a, b):
    callback_events.append((a, b))
    if b == 2:
        raise callback_error
    return None


def test_callback_failure_and_none_total():
    global callback_error, callback_events
    for failure in (ValueError("callback"), StopIteration("callback stop")):
        callback_error = failure
        callback_events = []
        selected = accumulate([1, 2, 3, 4], failing_combine)
        assert next(selected) == 1
        assert raises(failure.__class__, next, selected) is failure
        assert next(selected) is None
        assert next(selected) is None
        assert callback_events == [(1, 2), (1, 3), (None, 4)]
        assert next(selected, "done") == "done"


class AddTotal:
    def __init__(self, value):
        self.value = value

    def __add__(self, item):
        if item == 2:
            raise callback_error
        return AddTotal(self.value + item)


def test_addition_failure():
    global callback_error
    callback_error = ValueError("addition")
    initial = AddTotal(10)
    selected = accumulate([1, 2, 3], initial=initial)
    assert next(selected) is initial
    assert next(selected).value == 11
    assert raises(ValueError, next, selected) is callback_error
    assert next(selected).value == 14
    assert next(selected, "done") == "done"


class ReentrantTotal:
    def __init__(self, value):
        self.value = value

    def __add__(self, item):
        if item == 2:
            reentrant_events.append(next(reentrant_target).value)
            if callback_error is not None:
                raise callback_error
        return ReentrantTotal(self.value + item)


def test_addition_reentry():
    global callback_error, reentrant_target, reentrant_events
    for failure in (None, ValueError("outer addition")):
        callback_error = failure
        reentrant_events = []
        reentrant_target = accumulate([2, 3, 4], initial=ReentrantTotal(1))
        assert next(reentrant_target).value == 1
        if failure is None:
            assert next(reentrant_target).value == 3
            assert next(reentrant_target).value == 7
        else:
            assert raises(ValueError, next, reentrant_target) is failure
            assert next(reentrant_target).value == 8
        assert reentrant_events == [4]
        assert next(reentrant_target, "done") == "done"


def reentrant_combine(a, b):
    reentrant_events.append((a, b))
    if b == 2:
        reentrant_events.append(("inner", next(reentrant_target)))
        if callback_error is not None:
            raise callback_error
    return a + b


def test_callback_reentry():
    global callback_error, reentrant_target, reentrant_events
    for failure in (None, ValueError("outer callback"), StopIteration("outer stop")):
        callback_error = failure
        reentrant_events = []
        reentrant_target = accumulate([1, 2, 3, 4], reentrant_combine)
        assert next(reentrant_target) == 1
        if failure is None:
            assert next(reentrant_target) == 3
            assert next(reentrant_target) == 7  # Outer success overwrites inner 4.
        else:
            assert raises(failure.__class__, next, reentrant_target) is failure
            assert next(reentrant_target) == 8  # Failure retains inner update.
        assert next(reentrant_target, "done") == "done"
        assert reentrant_events[:3] == [(1, 2), (1, 3), ("inner", 4)]


class ReentrantSource:
    def __init__(self):
        self.position = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.position += 1
        value = self.position
        if value == 1:
            reentrant_events.append(next(reentrant_target))
        if value > 4:
            raise StopIteration
        return value


def test_source_reentry():
    global reentrant_target, reentrant_events
    reentrant_events = []
    reentrant_target = accumulate(ReentrantSource())
    assert next(reentrant_target) == 3  # Nested priming supplies total 2.
    assert next(reentrant_target) == 6
    assert next(reentrant_target) == 10
    assert next(reentrant_target, "done") == "done"
    assert reentrant_events == [2]
    reentrant_target = accumulate(ReentrantSource(), initial=0)
    assert next(reentrant_target) == 0
    assert next(reentrant_target) == 3
    assert next(reentrant_target) == 6


def test_hot_iteration():
    result = list(accumulate(range(240)))
    for i in range(240):
        assert result[i] == i * (i + 1) // 2


test_values_and_signatures()
test_laziness_and_source_recovery()
test_callback_failure_and_none_total()
test_addition_failure()
test_addition_reentry()
test_callback_reentry()
test_source_reentry()
test_hot_iteration()
