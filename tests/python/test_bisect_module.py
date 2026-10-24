# Portions adapted from CPython's Lib/test/test_bisect.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import bisect


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error:
        return
    raise AssertionError("expected exception")


def basic_searches():
    assert bisect.bisect is bisect.bisect_right
    assert bisect.insort is bisect.insort_right
    for data in ([], (), [1], [1, 1], [1, 2, 2, 3, 3, 3, 4]):
        for x in (0, 1, 1.5, 2, 2.5, 3, 4, 5):
            for lo in range(len(data) + 1):
                for hi in range(lo, len(data) + 1):
                    left = lo
                    right = lo
                    for i in range(lo, hi):
                        if data[i] < x:
                            left += 1
                        if not x < data[i]:
                            right += 1
                    assert bisect.bisect_left(data, x, lo, hi) == left
                    assert bisect.bisect_right(data, x, lo, hi) == right
            assert bisect.bisect_left(data, x) == bisect.bisect_left(data, x, hi=None)
            assert bisect.bisect_right(data, x) == bisect.bisect_right(data, x, hi=-1)
    assert bisect.bisect_left("abbc", "b") == 1
    assert bisect.bisect_right("abbc", "b") == 3


class Integer(int):
    pass


def bounds_and_calls():
    for search in (bisect.bisect_left, bisect.bisect_right, bisect.bisect):
        assert search(a=[10, 20, 30], x=25, lo=1, hi=3, key=None) == 2
        assert search([1, 2], 0, True, False) == 1
        assert search([], 0, Integer(3), Integer(2)) == 3
        assert search([], 0, 10**18, 0) == 10**18
        assert search([], 0, 2**63 - 1, -(2**63)) == 2**63 - 1
        assert search([1, 2], 0, hi=-2) == 0
        assert search([1, 2], 0, lo=4, hi=2) == 4
        assert search([], 0, True, False).__class__ is int
        raises(IndexError, search, [1], 2, hi=4)
    for function in (bisect.bisect_left, bisect.bisect_right,
                     bisect.insort_left, bisect.insort_right):
        raises(ValueError, function, [], 0, -1)
        for bad in (None, 1.0, "1"):
            raises(TypeError, function, [], 0, bad)
        for bad in (1.0, "1"):
            raises(TypeError, function, [], 0, 0, bad)
            raises(TypeError, function, [], 0, -1, bad)
        for bad in (2**63, -(2**63) - 1):
            raises(OverflowError, function, [], 0, bad)
            raises(OverflowError, function, [], 0, 0, bad)
        raises(TypeError, function)
        raises(TypeError, function, [])
        raises(TypeError, function, [], 0, 0, None, None)
        raises(TypeError, function, [], 0, unknown=1)
        raises(TypeError, function, [], 0, a=[])


def keyed_insertions():
    data = [2, -4, 6, -8]
    assert bisect.bisect_left(data, 4, key=abs) == 1
    assert bisect.bisect_right(data, 4, key=abs) == 2
    # Search x is already a key: abs must never be applied to it.
    assert bisect.bisect_left(data, -4, key=abs) == 0
    assert bisect.bisect_right(data, -4, key=abs) == 0
    for insert in (bisect.insort_left, bisect.insort_right, bisect.insort):
        target = []
        for x in (4, -2, 6, -4, 0, -8):
            assert insert(target, x, key=abs) is None
            for i in range(1, len(target)):
                assert abs(target[i - 1]) <= abs(target[i])
        assert insert(a=target, x=3, lo=1, hi=3, key=abs) is None
        assert target[2] == 3
    target = [1, 2, 3]
    assert bisect.insort(target, 2) is None
    assert target == [1, 2, 2, 3]
    target = [1, 2]
    bisect.insort_left(target, 9, lo=0, hi=0)
    assert target == [9, 1, 2]


basic_searches()
bounds_and_calls()
keyed_insertions()
