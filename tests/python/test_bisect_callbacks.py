# Portions adapted from CPython's Lib/test/test_bisect.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import bisect


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


events = []


class Sequence:
    def __init__(self):
        self.data = [1, 3, 5]

    def __len__(self):
        events.append("len")
        return len(self.data)

    def __getitem__(self, index):
        events.append(("get", index))
        return self.data[index]

    def insert(self, index, value):
        events.append(("insert", index, value))
        return "ignored"


def key(value):
    events.append(("key", value))
    return value


def callback_order():
    for search in (bisect.bisect_left, bisect.bisect_right):
        events.clear()
        assert search(Sequence(), 3, hi=0, key=key) == 0
        assert events == []
        events.clear()
        search(Sequence(), 3, key=key)
        assert events[0] == "len"
        assert events[1] == ("get", 1)
        assert events[2] == ("key", 3)
        assert len(events) == 5
    for insert in (bisect.insort_left, bisect.insort_right):
        events.clear()
        assert insert(Sequence(), 3, key=key) is None
        assert events[0] == ("key", 3)
        assert events[1] == "len"
        assert events[2] == ("get", 1)
        assert events[3] == ("key", 3)
        assert len(events) == 7
        events.clear()
        raises(ValueError, insert, Sequence(), 3, -1, key=key)
        assert events == [("key", 3)]
        # Dynamic tuple exception handlers are outside current VM support.
        for bounds in ((0, 1.5), (-1, 1.5), (None, None)):
            events.clear()
            raises(TypeError, insert, Sequence(), 3,
                   bounds[0], bounds[1], key=key)
            assert events == []
        events.clear()
        raises(OverflowError, insert, Sequence(), 3, 0, 2**63, key=key)
        assert events == []


class GetOnly:
    def __getitem__(self, index):
        return index


class LenOnly:
    def __len__(self):
        return 2


class LengthResult:
    def __init__(self, result):
        self.result = result

    def __len__(self):
        return self.result

    def __getitem__(self, index):
        return index


class LargeSequence:
    def __len__(self):
        return 2**63 - 1

    def __getitem__(self, index):
        return index

    def insert(self, index, value):
        self.last_index = index
        self.last_value = value


def sequence_bounds():
    for search in (bisect.bisect_left, bisect.bisect_right):
        raises(TypeError, search, GetOnly(), 3)
        assert search(GetOnly(), 3, hi=6) in (3, 4)
        for result in (-1, -(2**100)):
            raises(ValueError, search, LengthResult(result), 0)
        raises(TypeError, search, LengthResult(1.5), 0)
        raises(OverflowError, search, LengthResult(2**63), 0)
        assert search(LengthResult(True), 0) in (0, 1)
        raises(TypeError, search, 42, 0)
        raises(TypeError, search, LenOnly(), 0)
    data = LargeSequence()
    n = 2**63 - 1
    assert bisect.bisect_left(data, n - 3) == n - 3
    assert bisect.bisect_right(data, n - 3) == n - 2
    assert bisect.bisect_left(data, n - 3, n - 10, n) == n - 3
    bisect.insort_left(data, n - 100, n - 150, n - 50)
    assert data.last_index == n - 100
    assert data.last_value == n - 100
    bisect.insort_right(data, n - 100, n - 150, n - 50)
    assert data.last_index == n - 99


class Item:
    def __init__(self, value, name):
        self.value = value
        self.name = name

    def __lt__(self, other):
        events.append((self.name, other.name))
        return self.value < other.value

    def __eq__(self, other):
        raise AssertionError("bisect must use less-than only")


def item_key(item):
    return item.value


def comparisons_and_duplicates():
    old = Item(3, "old")
    new = Item(3, "new")
    events.clear()
    assert bisect.bisect_left([old], new) == 0
    assert events == [("old", "new")]
    events.clear()
    assert bisect.bisect_right([old], new) == 1
    assert events == [("new", "old")]
    data = [old]
    bisect.insort_left(data, new, key=item_key)
    assert data[0] is new
    assert data[1] is old
    data = [old]
    bisect.insort_right(data, new, key=item_key)
    assert data[0] is old
    assert data[1] is new


callback_order()
sequence_bounds()
comparisons_and_duplicates()
