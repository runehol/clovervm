# Python test set -- itertools zip_longest
#
# Portions adapted from CPython's Lib/test/test_itertools.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import itertools
from itertools import zip_longest


def raises(kind, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as caught:
        return caught
    raise AssertionError("expected exception")


class Source:
    def __init__(self, name, values, events):
        self.name = name
        self.values = iter(values)
        self.events = events

    def __iter__(self):
        self.events.append(("iter", self.name))
        return self

    def __next__(self):
        self.events.append(("next", self.name))
        value = next(self.values)
        if isinstance(value, BaseException):
            raise value
        return value


def test_values_and_signatures():
    assert list(zip_longest()) == []
    assert list(zip_longest(fillvalue=object())) == []
    assert list(zip_longest([])) == []
    assert list(zip_longest("abc")) == [("a",), ("b",), ("c",)]
    assert list(zip_longest("ABCD", "xy", fillvalue="-")) == [
        ("A", "x"), ("B", "y"), ("C", "-"), ("D", "-"),
    ]
    assert list(zip_longest([], [], [])) == []
    assert list(zip_longest([], [1, 2], [])) == [(None, 1, None), (None, 2, None)]
    fill = []
    selected = zip_longest([None, False, fill], [], fillvalue=fill)
    assert iter(selected) is selected
    rows = list(selected)
    assert rows == [(None, fill), (False, fill), (fill, fill)]
    for row in rows:
        assert row.__class__ is tuple
        assert row[1] is fill
    assert rows[2][0] is fill
    stop_value = StopIteration("data")
    row = next(zip_longest([stop_value], [], fillvalue=stop_value))
    assert row[0] is stop_value and row[1] is stop_value
    if hasattr(itertools, "_missing"):
        row = next(zip_longest([itertools._missing], [], fillvalue=itertools._missing))
        assert row[0] is itertools._missing and row[1] is itertools._missing
    raises(TypeError, zip_longest, 42)
    raises(TypeError, zip_longest, [], 42)
    raises(TypeError, zip_longest, [], unexpected=True)
    raises(TypeError, zip_longest, iterables=[])
    raises(TypeError, zip_longest, [], fillvalue=1, unexpected=True)
    # A second positional argument is another iterable, never a fill value.
    assert list(zip_longest([1], [2, 3])) == [(1, 2), (None, 3)]


acquisition_error = ValueError("acquisition")


class BadIterable:
    def __iter__(self):
        raise acquisition_error


def test_acquisition_and_order():
    events = []
    a = Source("a", [1, 2], events)
    b = Source("b", [3], events)
    c = Source("c", [4, 5], events)
    selected = zip_longest(a, b, c)
    assert events == [("iter", "a"), ("iter", "b"), ("iter", "c")]
    events.clear()
    assert next(selected) == (1, 3, 4)
    assert events == [("next", "a"), ("next", "b"), ("next", "c")]
    events.clear()
    assert next(selected) == (2, None, 5)
    assert events == [("next", "a"), ("next", "b"), ("next", "c")]
    events.clear()
    assert raises(StopIteration, next, selected).args == ()
    assert events == [("next", "a"), ("next", "c")]
    events.clear()
    assert next(selected, "done") == "done"
    assert events == []
    assert raises(ValueError, zip_longest, a, BadIterable(), c) is acquisition_error
    assert events == [("iter", "a")]
    events.clear()
    raises(TypeError, zip_longest, a, unexpected=True)
    assert events == []


def test_permanent_exhaustion_and_shared_source():
    events = []
    source = Source("short", [StopIteration("pause"), 99], events)
    selected = zip_longest(source, [1, 2, 3])
    assert list(selected) == [(None, 1), (None, 2), (None, 3)]
    assert events == [("iter", "short"), ("next", "short")]
    assert next(source) == 99
    source = Source("only", [StopIteration("pause"), 99], [])
    selected = zip_longest(source)
    assert raises(StopIteration, next, selected).args == ()
    assert next(selected, "done") == "done"
    assert next(source) == 99
    events = []
    source = Source("shared", [1, 2, 3], events)
    selected = zip_longest(source, source, fillvalue="F")
    assert list(selected) == [(1, 2), (3, "F")]
    expected = [("iter", "shared"), ("iter", "shared")]
    for _ in range(5):
        expected.append(("next", "shared"))
    assert events == expected
    # Exhaustion is recorded per slot, even with a resumable shared source.
    source = Source("shared", [StopIteration(), 7, 8], [])
    assert list(zip_longest(source, source)) == [(None, 7), (None, 8)]


def test_source_errors_and_consumption():
    for slot in range(3):
        for kind in (ValueError, KeyboardInterrupt):
            events = []
            error = kind("source")
            sources = []
            for index in range(3):
                if index == slot:
                    values = [index, error, 90 + index]
                else:
                    values = [index, 10 + index, 20 + index]
                sources.append(Source(index, values, events))
            selected = zip_longest(*sources)
            assert next(selected) == (0, 1, 2)
            events.clear()
            assert raises(kind, next, selected) is error
            expected = []
            for index in range(slot + 1):
                expected.append(("next", index))
            assert events == expected
            events.clear()
            assert next(selected, "done") == "done"
            assert events == []
            # Partial-row values were consumed; later sources were untouched.
            for index in range(3):
                if index < slot:
                    assert next(sources[index]) == 20 + index
                elif index == slot:
                    assert next(sources[index]) == 90 + index
                else:
                    assert next(sources[index]) == 10 + index
    error = RuntimeError("after short source")
    selected = zip_longest([], Source("bad", [error, 5], []), [1])
    assert raises(RuntimeError, next, selected) is error
    assert next(selected, "done") == "done"


reentrant_target = None
reentrant_events = []


class ReentrantSource:
    def __init__(self, values, before=False, stop=False):
        self.values = iter(values)
        self.entered = False
        self.before = before
        self.stop = stop

    def __iter__(self):
        return self

    def __next__(self):
        if self.entered:
            return next(self.values)
        self.entered = True
        if self.before:
            reentrant_events.append(next(reentrant_target, "done"))
            if self.stop:
                raise StopIteration("outer")
            return next(self.values)
        value = next(self.values)
        reentrant_events.append(next(reentrant_target, "done"))
        return value


def test_safe_source_reentry():
    global reentrant_target, reentrant_events
    for before in (False, True):
        reentrant_events = []
        source = ReentrantSource([1, 2, 3], before=before)
        reentrant_target = zip_longest(source, [8, 9, 10])
        if before:
            assert next(reentrant_target) == (2, 9)
            assert reentrant_events == [(1, 8)]
        else:
            assert next(reentrant_target) == (1, 9)
            assert reentrant_events == [(2, 8)]
        assert list(reentrant_target) == [(3, 10)]
    reentrant_target = None


reentrant_error = ValueError("nested")


class CatchingSource:
    def __init__(self, item):
        self.item = item
        self.entered = False

    def __iter__(self):
        return self

    def __next__(self):
        if self.entered:
            raise reentrant_error
        self.entered = True
        assert raises(ValueError, next, reentrant_target) is reentrant_error
        return self.item


def test_caught_nested_error():
    global reentrant_target
    item = []
    source = CatchingSource(item)
    reentrant_target = zip_longest(source)
    row = next(reentrant_target)
    assert row[0] is item
    assert next(reentrant_target, "done") == "done"
    reentrant_target = None


class MutableSource:
    def __iter__(self):
        return self

    def __next__(self):
        return 1


def replacement_next(self):
    return 2


def exhausted_next(self):
    raise StopIteration("stop")


def test_source_next_invalidation():
    selected = zip_longest(MutableSource())
    for _ in range(300):
        assert next(selected) == (1,)
    MutableSource.__next__ = replacement_next
    for _ in range(300):
        assert next(selected) == (2,)
    MutableSource.__next__ = exhausted_next
    assert raises(StopIteration, next, selected).args == ()
    MutableSource.__next__ = replacement_next
    assert next(selected, "done") == "done"


def test_managed_sticky_reentry():
    global reentrant_target, reentrant_events
    if not hasattr(itertools, "_missing"):
        return
    # These native CPython 3.14.5 cases can underflow numactive, corrupt
    # references or crash; they intentionally exercise only the managed code.
    for values in ([], [1]):
        reentrant_events = []
        reentrant_target = zip_longest(ReentrantSource(values))
        if values:
            assert next(reentrant_target) == (1,)
            assert reentrant_events == ["done"]
        else:
            assert next(reentrant_target, "done") == "done"
        assert next(reentrant_target, "done") == "done"
    for stop in (False, True):
        reentrant_events = []
        reentrant_target = zip_longest(ReentrantSource([], before=True, stop=stop))
        assert raises(StopIteration, next, reentrant_target).args == ()
        assert reentrant_events == ["done"]
        assert next(reentrant_target, "done") == "done"
    # Nested advancement retires the outer call's source slot while another
    # source remains live. Keep the outer source rooted despite that release;
    # the native 3.14.5 implementation can double-decref it in this case.
    reentrant_events = []
    other = iter([8, 9])
    reentrant_target = zip_longest(ReentrantSource([], before=True), other)
    assert next(reentrant_target, "done") == "done"
    assert reentrant_events == [(None, 8)]
    assert next(reentrant_target, "done") == "done"
    assert next(other) == 9
    # If a later slot also stops after nested closure, discard the partial
    # outer row without decrementing the active count again.
    reentrant_events = []
    reentrant_target = zip_longest(ReentrantSource([1]), [])
    assert next(reentrant_target, "done") == "done"
    assert reentrant_events == ["done"]
    assert next(reentrant_target, "done") == "done"
    reentrant_target = None


def test_hot_iteration_and_retained_rows():
    rows = list(zip_longest(range(720), range(360), fillvalue="F"))
    assert len(rows) == 720
    for index in range(720):
        if index < 360:
            assert rows[index] == (index, index)
        else:
            assert rows[index] == (index, "F")
    rows = list(zip_longest([[1], [2]], [[3]]))
    assert rows == [([1], [3]), ([2], None)]
    rows[0][0].append(9)
    assert rows == [([1, 9], [3]), ([2], None)]


test_values_and_signatures()
test_acquisition_and_order()
test_permanent_exhaustion_and_shared_source()
test_source_errors_and_consumption()
test_safe_source_reentry()
test_caught_nested_error()
test_source_next_invalidation()
test_managed_sticky_reentry()
test_hot_iteration_and_retained_rows()
