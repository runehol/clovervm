# Python test set -- itertools batched
#
# Portions adapted from CPython's Lib/test/test_itertools.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import itertools
from itertools import batched


def raises(kind, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as caught:
        return caught
    raise AssertionError("expected exception")


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
    assert list(batched("ABCDEFG", 3)) == [("A", "B", "C"), ("D", "E", "F"), ("G",)]
    assert list(batched(iterable=[1, 2, 3], n=2)) == [(1, 2), (3,)]
    assert list(batched([1, 2, 3], True)) == [(1,), (2,), (3,)]
    assert list(batched([], 3, strict=True)) == []
    assert list(batched([None, False, []], 3, strict=True)) == [(None, False, [])]
    assert list(batched(range(6), 2, strict=True)) == [(0, 1), (2, 3), (4, 5)]
    raises(TypeError, batched)
    raises(TypeError, batched, [])
    raises(TypeError, batched, [], 2, False)
    raises(TypeError, batched, [], 2, iterable=[])
    raises(TypeError, batched, [], 2, unexpected=True)
    raises(TypeError, batched, 42, 2)
    for size in (False, 0, -1, -(2**63)):
        raises(ValueError, batched, [], size)
    for size in (2**63, -(2**63) - 1):
        raises(OverflowError, batched, [], size)
    for size in (None, "2", 2.0):
        raises(TypeError, batched, [], size)
    # The largest valid size is accepted at construction without allocation.
    assert iter(batched([], 2**63 - 1)) is not None
    for size in range(1, 6):
        for length in range(15):
            batches = list(batched(range(length), size))
            flattened = []
            for batch in batches:
                assert batch.__class__ is tuple
                assert 1 <= len(batch) <= size
                flattened.extend(batch)
            assert flattened == list(range(length))
            for batch in batches[:-1]:
                assert len(batch) == size


validation_events = []
validation_error = ValueError("validation")


class Strict:
    def __init__(self, failure=None):
        self.failure = failure

    def __bool__(self):
        validation_events.append("strict")
        if self.failure is not None:
            raise self.failure
        return True


class IntSubclass(int):
    def __index__(self):
        raise AssertionError("must use integer payload")

    def __int__(self):
        raise AssertionError("must use integer payload")


class CustomIndex:
    def __index__(self):
        raise AssertionError("custom index is outside this slice")


def test_validation_order():
    global validation_events
    validation_events = []
    source = Source([], validation_events)
    selected = batched(source, IntSubclass(2), strict=Strict())
    assert validation_events == ["strict", "iter"]
    assert next(selected, "done") == "done"
    validation_events = []
    assert raises(ValueError, batched, source, 0, strict=Strict(validation_error)) is validation_error
    assert validation_events == ["strict"]
    validation_events = []
    raises(ValueError, batched, source, -1, strict=Strict())
    assert validation_events == ["strict"]
    for size in (2**63, -(2**63)-1):
        validation_events = []
        raises(OverflowError, batched, source, size, strict=Strict(validation_error))
        assert validation_events == []
    for size in (1.5, None):
        validation_events = []
        raises(TypeError, batched, source, size, strict=Strict(validation_error))
        assert validation_events == []
    # No local imitation of the unsupported general index protocol.
    if hasattr(itertools, "_missing"):
        raises(TypeError, batched, [], CustomIndex())
        # Intentional allocation-strategy difference: the managed iterator
        # handles valid huge n incrementally, rather than preallocating n slots.
        # These cases also exceed the VM range constructor's narrower bounds.
        for size in (2**60, 2**63 - 1):
            assert next(batched([], size), "done") == "done"
            assert next(batched([1], size)) == (1,)
            raises(ValueError, next, batched([1], size, strict=True))


def test_laziness_and_terminal_states():
    for strict in (False, True):
        events = []
        source = Source([1, 2, 3], events)
        selected = batched(source, 2, strict=strict)
        assert iter(selected) is selected
        assert events == ["iter"]
        assert next(selected) == (1, 2)
        assert events == ["iter", "next", "next"]
        if strict:
            error = raises(ValueError, next, selected)
            assert error.args == ("batched(): incomplete batch",)
        else:
            assert next(selected) == (3,)
            before = len(events)
            assert next(selected, "done") == "done"
            assert len(events) == before + 1
        before = len(events)
        assert next(selected, "done") == "done"
        assert next(selected, "done") == "done"
        assert len(events) == before
    for failure in (ValueError("source"), KeyboardInterrupt("interrupt")):
        events = []
        source = Source([1, failure, 2, 3], events)
        selected = batched(source, 2)
        assert raises(failure.__class__, next, selected) is failure
        assert next(selected, "done") == "done"
        assert events == ["iter", "next", "next"]
        assert next(source) == 2  # Partial batch was discarded.


def test_resumable_stop():
    stop = StopIteration("pause")
    for strict in (False, True):
        events = []
        source = Source([1, stop, 2, 3], events)
        selected = batched(source, 2, strict=strict)
        if strict:
            raises(ValueError, next, selected)
            assert next(selected, "done") == "done"
            assert next(source) == 2
        else:
            assert next(selected) == (1,)
            assert next(selected) == (2, 3)
            assert next(selected, "done") == "done"
        events = []
        source = Source([stop, 1, 2], events)
        selected = batched(source, 2, strict=strict)
        assert raises(StopIteration, next, selected).args == ()
        assert next(selected, "done") == "done"
        assert events == ["iter", "next"]
        assert next(source) == 1


reentrant_target = None
reentrant_events = []


class ReentrantSource:
    def __init__(self, values):
        self.values = iter(values)
        self.reentered = False

    def __iter__(self):
        return self

    def __next__(self):
        value = next(self.values)
        if isinstance(value, BaseException):
            raise value
        if not self.reentered:
            self.reentered = True
            reentrant_events.append(next(reentrant_target, "done"))
        return value


def test_source_reentry():
    global reentrant_target, reentrant_events
    for strict in (False, True):
        reentrant_events = []
        source = ReentrantSource([1, 2, 3, 4])
        reentrant_target = batched(source, 2, strict=strict)
        assert next(reentrant_target) == (1, 4)
        assert reentrant_events == [(2, 3)]
        assert next(reentrant_target, "done") == "done"
        # A nested empty batch closes self, but the outer source stays alive.
        # Retain source explicitly for the native CPython 3.14.5 reference:
        # the unretained variant can crash that interpreter after reentry.
        reentrant_events = []
        source = ReentrantSource([1])
        reentrant_target = batched(source, 2, strict=strict)
        if strict:
            raises(ValueError, next, reentrant_target)
        else:
            assert next(reentrant_target) == (1,)
        assert reentrant_events == ["done"]
        assert next(reentrant_target, "done") == "done"
        # Nested closure remains terminal even if the retained local source
        # resumes and lets the outer call finish a full batch.
        reentrant_events = []
        source = ReentrantSource([1, StopIteration("pause"), 2, 3])
        reentrant_target = batched(source, 2, strict=strict)
        assert next(reentrant_target) == (1, 2)
        assert reentrant_events == ["done"]
        assert next(reentrant_target, "done") == "done"
        assert next(source) == 3


def test_managed_source_retention():
    global reentrant_target, reentrant_events
    if not hasattr(itertools, "_missing"):
        return
    # Covers the unsafe native-reference variant using actual managed roots,
    # including under reclamation at every safepoint in the C++ harness.
    for strict in (False, True):
        reentrant_events = []
        reentrant_target = batched(ReentrantSource([1]), 2, strict=strict)
        if strict:
            raises(ValueError, next, reentrant_target)
        else:
            assert next(reentrant_target) == (1,)
        assert reentrant_events == ["done"]
        assert next(reentrant_target, "done") == "done"


def test_hot_iteration():
    batches = list(batched(range(720), 3, strict=True))
    assert len(batches) == 240
    for i in range(240):
        assert batches[i] == (3*i, 3*i+1, 3*i+2)


test_values_and_signatures()
test_validation_order()
test_laziness_and_terminal_states()
test_resumable_stop()
test_source_reentry()
test_managed_source_retention()
test_hot_iteration()
