# Python test set -- itertools selection iterators
#
# Portions adapted from CPython's Lib/test/test_itertools.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

from itertools import compress, filterfalse, takewhile, dropwhile


def raises(kind, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as caught:
        return caught
    raise AssertionError("expected exception")


class Source:
    def __init__(self, values, events, name):
        self.values = iter(values)
        self.events = events
        self.name = name

    def __iter__(self):
        self.events.append((self.name, "iter"))
        return self

    def __next__(self):
        self.events.append((self.name, "next"))
        value = next(self.values)
        if isinstance(value, BaseException):
            raise value
        return value


def even(value):
    return value % 2 == 0


def under_ten(value):
    return value < 10


def identity(value):
    return value


def false(value):
    return False


def true(value):
    return True


def test_values_and_signatures():
    assert list(compress(data=[0, 1, 2, 3, 4], selectors=[1, 0, 1, 0, 1])) == [0, 2, 4]
    assert list(compress([1, 2, 3], [0, 1])) == [2]
    assert list(compress([], [1])) == []
    assert list(compress([1], [])) == []
    assert list(filterfalse(even, [0, 1, 2, 3, 4, 5])) == [1, 3, 5]
    assert list(filterfalse(None, [0, 1, None, [], (), "", "x"])) == [0, None, [], (), ""]
    # CPython special-cases the bool identity; this needs no bool constructor.
    assert list(filterfalse(bool, [0, 1, None, [], 2])) == [0, None, []]
    data = [1, 3, 5, 20, 2, 4]
    assert list(takewhile(under_ten, data)) == [1, 3, 5]
    assert list(dropwhile(under_ten, data)) == [20, 2, 4]
    for factory in (filterfalse, takewhile, dropwhile):
        assert list(factory(42, [])) == []
        raises(TypeError, next, factory(42, [1]))
        raises(TypeError, factory)
        raises(TypeError, factory, identity)
        raises(TypeError, factory, identity, [], 1)
        raises(TypeError, factory, identity, 42)
    raises(TypeError, filterfalse, function=None, iterable=[])
    raises(TypeError, takewhile, predicate=identity, iterable=[])
    raises(TypeError, dropwhile, predicate=identity, iterable=[])
    raises(TypeError, compress)
    raises(TypeError, compress, [])
    raises(TypeError, compress, [], [], [])
    raises(TypeError, compress, [], [], data=[])
    raises(TypeError, compress, 42, [])
    raises(TypeError, compress, [], 42)


def test_lazy_consumption():
    events = []
    data = Source([1, 2, 3], events, "data")
    selectors = Source([0, 1], events, "selectors")
    selected = compress(data, selectors)
    assert iter(selected) is selected
    assert events == [("data", "iter"), ("selectors", "iter")]
    assert next(selected) == 2
    assert events == [("data", "iter"), ("selectors", "iter"),
                      ("data", "next"), ("selectors", "next"),
                      ("data", "next"), ("selectors", "next")]
    assert next(selected, "done") == "done"
    assert events[-2:] == [("data", "next"), ("selectors", "next")]
    before = len(events)
    assert next(selected, "done") == "done"
    # No terminal flag: it probes data again, but data exhaustion skips selectors.
    assert len(events) == before + 1

    source = iter([1, 3, 20, 2])
    selected = takewhile(under_ten, source)
    assert list(selected) == [1, 3]
    assert next(source) == 2  # The failing item, 20, was consumed.
    assert next(selected, "done") == "done"
    for case in ((filterfalse, false), (takewhile, true), (dropwhile, false)):
        events = []
        source = Source([7], events, "source")
        selected = case[0](case[1], source)
        assert iter(selected) is selected
        assert events == [("source", "iter")]
        assert next(selected) == 7
        assert events == [("source", "iter"), ("source", "next")]


error = ValueError("selection failure")


class BadTruth:
    def __init__(self, failure=error):
        self.failure = failure

    def __bool__(self):
        raise self.failure


class BadIterable:
    def __iter__(self):
        raise error


def test_construction_failure_order():
    events = []
    source = Source([], events, "source")
    assert raises(ValueError, compress, BadIterable(), source) is error
    assert events == []
    assert raises(ValueError, compress, source, BadIterable()) is error
    assert events == [("source", "iter")]
    for factory in (filterfalse, takewhile, dropwhile):
        # Iteration setup fails before an invalid callback is ever called.
        assert raises(ValueError, factory, 42, BadIterable()) is error


def test_compress_recovery():
    for failure in (error, StopIteration("recoverable stop")):
        for side in (0, 1, 2):
            events = []
            data_values = [1, 2]
            selectors_values = [1, 1]
            if side == 0:
                data_values = [failure, 2]
            elif side == 1:
                selectors_values = [failure, 1]
            else:
                selectors_values = [BadTruth(failure), 1]
            selected = compress(Source(data_values, events, "data"),
                                Source(selectors_values, events, "selectors"))
            assert raises(failure.__class__, next, selected) is failure
            assert next(selected) == 2
            expected = [("data", "iter"), ("selectors", "iter"), ("data", "next")]
            if side != 0:
                expected.append(("selectors", "next"))
            expected.append(("data", "next"))
            expected.append(("selectors", "next"))
            assert events == expected


callback_error = None
callback_seen = []


def recovering_predicate(item):
    callback_seen.append(item)
    if item == 1:
        if callback_error is not None:
            raise callback_error
        return BadTruth()
    return item == 0


def test_predicate_recovery():
    global callback_error, callback_seen
    for factory in (filterfalse, takewhile, dropwhile):
        for failure in (error, StopIteration("predicate stop"), None):
            callback_error = failure
            callback_seen = []
            selected = factory(recovering_predicate, [1, 0, 2, 3])
            expected_error = failure
            if expected_error is None:
                expected_error = error
            assert raises(expected_error.__class__, next, selected) is expected_error
            if factory is takewhile:
                assert next(selected) == 0
                assert next(selected, "done") == "done"
                assert next(selected, "done") == "done"
                assert callback_seen == [1, 0, 2]
            else:
                assert next(selected) == 2
                assert next(selected) == 3
                if factory is dropwhile:
                    assert callback_seen == [1, 0, 2]
                else:
                    assert callback_seen == [1, 0, 2, 3]


def test_source_recovery():
    for case in ((filterfalse, false), (takewhile, true), (dropwhile, false)):
        for failure in (error, StopIteration("source stop")):
            events = []
            selected = case[0](case[1], Source([failure, 7, failure, 8], events, "source"))
            assert raises(failure.__class__, next, selected) is failure
            assert next(selected) == 7
            assert raises(failure.__class__, next, selected) is failure
            assert next(selected) == 8


def test_truth_failure_recovery():
    for factory in (filterfalse, takewhile, dropwhile):
        for failure in (error, StopIteration("truth stop")):
            source = iter([BadTruth(failure), 0, 2])
            selected = factory(identity, source)
            assert raises(failure.__class__, next, selected) is failure
            if factory is takewhile:
                assert next(selected, "done") == "done"
                assert next(source) == 2
            else:
                assert next(selected) == 0
                if factory is dropwhile:
                    assert next(selected) == 2
                else:
                    assert next(selected, "done") == "done"
    item = BadTruth()
    selected = dropwhile(identity, [0, item])
    assert next(selected) == 0
    assert next(selected) is item  # No callback/truth test in pass-through mode.


reentrant_target = None
reentrant_events = []


def reentrant_predicate(item):
    reentrant_events.append(item)
    if item == 1:
        reentrant_events.append(("inner", next(reentrant_target, "done")))
        return True
    return False


def test_predicate_reentry():
    global reentrant_target, reentrant_events
    for factory in (takewhile, dropwhile, filterfalse):
        reentrant_events = []
        reentrant_target = factory(reentrant_predicate, [1, 2, 3])
        if factory is takewhile:
            assert next(reentrant_target) == 1
            assert next(reentrant_target, "done") == "done"
            assert reentrant_events == [1, 2, ("inner", "done")]
        else:
            assert next(reentrant_target) == 3
            assert next(reentrant_target, "done") == "done"
            if factory is dropwhile:
                assert reentrant_events == [1, 2, ("inner", 2)]
            else:
                assert reentrant_events == [1, 2, ("inner", 2), 3]


class ReentrantTruth:
    def __bool__(self):
        reentrant_events.append(next(reentrant_target, "done"))
        return True


def test_truth_reentry():
    global reentrant_target, reentrant_events
    reentrant_events = []
    reentrant_target = compress([1, 2, 3], [ReentrantTruth(), 1, 1])
    assert next(reentrant_target) == 1
    assert reentrant_events == [2]
    assert next(reentrant_target) == 3
    for factory in (filterfalse, takewhile, dropwhile):
        reentrant_events = []
        item = ReentrantTruth()
        reentrant_target = factory(identity, [item, 0, 3])
        if factory is takewhile:
            assert next(reentrant_target) is item
            assert reentrant_events == ["done"]
            assert next(reentrant_target, "done") == "done"
        elif factory is dropwhile:
            assert next(reentrant_target) == 3
            assert reentrant_events == [0]
        else:
            assert next(reentrant_target, "done") == "done"
            assert reentrant_events == [0]


def test_hot_iteration():
    # Enough calls to exercise ordinary tiering in JIT-enabled test builds.
    assert list(compress(range(240), range(240))) == list(range(1, 240))
    assert list(filterfalse(even, range(240))) == list(range(1, 240, 2))
    assert list(takewhile(true, range(240))) == list(range(240))
    assert list(dropwhile(false, range(240))) == list(range(240))
    assert list(dropwhile(true, range(240))) == []


test_values_and_signatures()
test_lazy_consumption()
test_construction_failure_order()
test_compress_recovery()
test_predicate_recovery()
test_source_recovery()
test_truth_failure_recovery()
test_predicate_reentry()
test_truth_reentry()
test_hot_iteration()
