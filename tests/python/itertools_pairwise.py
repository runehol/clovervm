# Python test set -- itertools pairwise
#
# Portions adapted from CPython's Lib/test/test_itertools.py at v3.14.5,
# including test_pairwise_reenter and test_pairwise_reenter2.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import itertools
from itertools import pairwise


def raises(kind, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as caught:
        return caught
    raise AssertionError("expected exception")


def test_values_and_signatures():
    assert list(pairwise([])) == []
    assert list(pairwise([1])) == []
    assert list(pairwise([1, 2])) == [(1, 2)]
    assert list(pairwise([1, 2, 3, 4])) == [(1, 2), (2, 3), (3, 4)]
    assert list(pairwise([None, None, 1])) == [(None, None), (None, 1)]
    first = []
    second = []
    pairs = pairwise([first, second, 3])
    assert iter(pairs) is pairs
    result = next(pairs)
    assert result.__class__ is tuple
    assert result[0] is first
    assert result[1] is second
    later = next(pairs)
    assert later[0] is second
    assert result == ([], [])  # Later calls cannot overwrite retained tuples.
    assert result is not later
    raises(TypeError, pairwise)
    raises(TypeError, pairwise, [], [])
    raises(TypeError, pairwise, iterable=[])
    raises(TypeError, pairwise, 42)


def test_arbitrary_input_objects():
    markers = [None, False, True, object()]
    if hasattr(itertools, "_missing"):
        # A module helper sentinel can also be ordinary input data. It must
        # never be confused with iterator control state. CPython has no such
        # exported sentinel; the other marker cases run there unchanged.
        markers.append(itertools._missing)
    for marker in markers:
        for values in ([1, marker, 2, 3], [marker, marker, marker],
                       [1, 2, marker, 3], [marker, 1, marker, 2]):
            pairs = list(pairwise(values))
            assert len(pairs) == len(values) - 1
            index = 0
            for pair in pairs:
                assert pair[0] is values[index]
                assert pair[1] is values[index + 1]
                index += 1


class Source:
    def __init__(self, values):
        self.values = iter(values)
        self.iter_calls = 0
        self.next_calls = 0

    def __iter__(self):
        self.iter_calls += 1
        return self

    def __next__(self):
        self.next_calls += 1
        value = next(self.values)
        if isinstance(value, BaseException):
            raise value
        return value


def test_consumption_and_errors():
    source = Source([1, 2, 3])
    pairs = pairwise(source)
    assert source.iter_calls == 1
    assert source.next_calls == 0
    assert next(pairs) == (1, 2)
    assert source.next_calls == 2
    assert next(pairs) == (2, 3)
    assert source.next_calls == 3
    assert next(pairs, "done") == "done"
    assert source.next_calls == 4
    assert next(pairs, "done") == "done"
    assert source.next_calls == 4
    for failure in (ValueError("source failed"), StopIteration("source stopped", 42)):
        for prefix in ([], [1], [1, 2]):
            source = Source(prefix + [failure, 8, 9])
            pairs = pairwise(source)
            if len(prefix) == 2:
                assert next(pairs) == (1, 2)
            assert raises(failure.__class__, next, pairs) is failure
            calls = source.next_calls
            for attempt in (0, 1):
                caught = raises(StopIteration, next, pairs)
                assert caught.args == ()
                assert source.next_calls == calls
            # pairwise closes itself, without consuming or closing the source.
            assert next(source) == 8


reentrant_target = None


class Reentrant:
    def __init__(self, reenter_at):
        self.count = 0
        self.reenter_at = reenter_at

    def __iter__(self):
        return self

    def __next__(self):
        self.count += 1
        if self.count in self.reenter_at:
            return next(reentrant_target)
        return [self.count]


def test_reentry():
    global reentrant_target
    for case in (
        ([1], [(([2], [3]), [4]), ([4], [5])]),
        ([2], [([1], ([1], [3])), (([1], [3]), [4]), ([4], [5])]),
        ([3], [([1], [2]), ([2], ([2], [4])), (([2], [4]), [5]), ([5], [6])]),
        ([1, 2], [((([3], [4]), [5]), [6]), ([6], [7])]),
        ([1, 3], [(([2], ([2], [4])), [5]), ([5], [6])]),
        ([1, 4], [(([2], [3]), (([2], [3]), [5])),
                  ((([2], [3]), [5]), [6]), ([6], [7])]),
        ([2, 3], [([1], ([1], ([1], [4]))),
                  (([1], ([1], [4])), [5]), ([5], [6])]),
    ):
        reentrant_target = pairwise(Reentrant(case[0]))
        for expected in case[1]:
            assert next(reentrant_target) == expected


class ReentrantExhaustion:
    def __init__(self, limit):
        self.count = 0
        self.limit = limit

    def __iter__(self):
        return self

    def __next__(self):
        if self.count >= self.limit:
            raise StopIteration
        self.count += 1
        if self.count == 1:
            return next(reentrant_target, None)
        return [self.count]


def test_reentrant_exhaustion():
    global reentrant_target
    for limit in (1, 2, 3, 4):
        source = ReentrantExhaustion(limit)
        reentrant_target = pairwise(source)
        if limit == 4:
            assert list(reentrant_target) == [(([2], [3]), [4])]
        else:
            assert list(reentrant_target) == []
        calls = source.count
        assert next(reentrant_target, "done") == "done"
        assert source.count == calls


def test_hot_iteration():
    pairs = list(pairwise(range(240)))
    assert len(pairs) == 239
    position = 0
    for pair in pairs:
        assert pair == (position, position + 1)
        position += 1
    assert position == 239


test_values_and_signatures()
test_arbitrary_input_objects()
test_consumption_and_errors()
test_reentry()
test_reentrant_exhaustion()
test_hot_iteration()
