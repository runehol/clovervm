# Python test set -- itertools cycle
#
# Basic cases adapted from CPython's Lib/test/test_itertools.py at v3.14.10.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import itertools
from itertools import cycle


def raises(kind, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as error:
        return error
    raise AssertionError("expected exception")


def take(selected, count):
    result = []
    for i in range(count):
        result.append(next(selected))
    return result


class Source:
    def __init__(self, values):
        self.values = iter(values)
        self.iter_calls = 0
        self.calls = 0

    def __iter__(self):
        self.iter_calls += 1
        return self

    def __next__(self):
        self.calls += 1
        value = next(self.values)
        if isinstance(value, BaseException):
            raise value
        return value


def test_basic():
    if hasattr(itertools, "__all__"):
        assert "cycle" in itertools.__all__
    assert list(itertools.islice(cycle("abc"), 10)) == list("abcabcabca")
    assert list(cycle([])) == []
    raises(TypeError, cycle)
    raises(TypeError, cycle, [], [])
    raises(TypeError, cycle, iterable=[])
    raises(TypeError, cycle, [], bad=1)
    raises(TypeError, cycle, 42)
    for values in ((), [], "", b"", bytearray()):
        selected = cycle(values)
        assert raises(StopIteration, next, selected).args == ()
        assert raises(StopIteration, next, selected).args == ()
    for values in ([1, 2], (1, 2), range(1, 3), b"\x01\x02", bytearray(b"\x01\x02")):
        selected = cycle(values)
        assert take(selected, 5) == [1, 2, 1, 2, 1]
    if hasattr(itertools, "_missing"):
        selected = cycle([itertools._missing])
        assert next(selected) is itertools._missing
        assert next(selected) is itertools._missing
    source = Source([None, False, [], StopIteration("pause"), 99])
    selected = cycle(source)
    assert iter(selected) is selected
    assert source.iter_calls == 1 and source.calls == 0
    a = next(selected)
    b = next(selected)
    c = next(selected)
    assert a is None and b is False and c == []
    c.append(7)
    assert next(selected) is a
    assert next(selected) is b
    assert next(selected) is c and c == [7]
    assert source.calls == 4
    assert next(source) == 99
    for args in ((), ([],), (1, 2)):
        assert selected.__init__(*args, bad=3) is None
    assert next(selected) is a
    assert selected.__init__() is None
    assert selected.__init__([99]) is None
    assert next(selected) is b
    empty = cycle(Source([StopIteration("payload"), 8]))
    for attempt in range(3):
        assert raises(StopIteration, next, empty).args == ()
    class FailingAcquisition:
        def __iter__(self):
            raise ValueError("acquisition")
    assert raises(ValueError, cycle, FailingAcquisition()).args == ("acquisition",)
    for prefix in ([], [1]):
        failure = ValueError("retry")
        source = Source(prefix + [failure, 7])
        selected = cycle(source)
        for item in prefix:
            assert next(selected) == item
        assert raises(ValueError, next, selected) is failure
        assert next(selected) == 7
        assert next(selected) == (prefix + [7])[0]
    failure = ValueError("input")
    source = Source([1, failure, 2])
    selected = cycle(source)
    assert next(selected) == 1
    assert raises(ValueError, next, selected) is failure
    assert next(selected) == 2
    assert [next(selected), next(selected), next(selected)] == [1, 2, 1]
    assert source.calls == 4


class Reentrant:
    def __init__(self, values, at, mode):
        self.values = iter(values)
        self.calls = 0
        self.at = at
        self.mode = mode
        self.nested = None

    def __iter__(self):
        return self

    def __next__(self):
        self.calls += 1
        if self.calls == self.at:
            self.nested = next(self.target, "empty")
            if self.mode == 1:
                return 99
            if self.mode == 2:
                raise ValueError("outer")
            if self.mode == 3:
                raise StopIteration("outer")
            return self.nested
        return next(self.values)


def test_reentry():
    for case in (
        ([1, 2], 1, 0, [1, 2, 1, 1, 2, 1]),
        ([], 1, 1, [99, 99, 99, 99, 99, 99]),
        ([1], 2, 3, [1, 1, 1, 1, 1, 1]),
        ([1, 2], 2, 1, [1, 99, 1, 2, 99, 1]),
    ):
        source = Reentrant(case[0], case[1], case[2])
        selected = cycle(source)
        source.target = selected
        assert take(selected, 6) == case[3]
    source = Reentrant([], 1, 2)
    selected = cycle(source)
    source.target = selected
    raises(ValueError, next, selected)
    assert next(selected, "done") == "done"
    assert source.calls == 2


def test_mutation_cache_and_roots():
    first = []
    values = [first]
    selected = cycle(values)
    assert next(selected) is first
    values.append(8)
    assert next(selected) == 8
    assert next(selected) is first
    values.append(9)  # Already replaying: never reopen the exhausted source.
    assert next(selected) == 8
    first.append(4)
    assert next(selected) == [4]
    source = iter([1, 2, 3])
    left = cycle(source)
    right = cycle(source)
    assert next(left) == 1
    assert next(right) == 2
    assert next(left) == 3
    assert next(right) == 2
    assert next(left) == 1
    assert next(right) == 2
    source = Source([1, 2])
    selected = cycle(source)
    assert next(selected) == 1
    def replacement(self):
        return 9
    original = Source.__next__
    Source.__next__ = replacement
    assert next(selected) == 9
    Source.__next__ = original
    assert next(selected) == 2
    assert next(selected) == 1
    assert next(selected) == 9
    selected = cycle([[1], [2], [3]])
    for i in range(900):
        assert next(selected) == [i % 3 + 1]
    selected = cycle(itertools.count())
    for i in range(600):
        assert next(selected) == i
    # Cyclic managed graph, source reachable only through cycle during calls.
    source = Reentrant([[5]], 1, 0)
    source.target = cycle(source)
    selected = source.target
    source = None
    assert next(selected) == [5]
    for i in range(100):
        assert next(selected) == [5]


test_basic()
test_reentry()
test_mutation_cache_and_roots()
