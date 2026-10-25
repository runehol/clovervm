# Python test set -- statistics subset
#
# Portions adapted from CPython's Lib/test/test_statistics.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import math
import statistics as s


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


functions = (s.median, s.median_low, s.median_high, s.mode, s.multimode)
for name in (
    "StatisticsError", "median", "median_low", "median_high", "mode", "multimode"
):
    assert name in s.__all__
assert issubclass(s.StatisticsError, ValueError)
for function in functions:
    assert function(data=[2]) == function([2])
    raises(TypeError, function)
    raises(TypeError, function, [], [])
    raises(TypeError, function, [], data=[])
    raises(TypeError, function, other=[])
    raises(TypeError, function, None)
    raises(TypeError, function, 42)

for function in (s.median, s.median_low, s.median_high):
    error = raises(s.StatisticsError, function, [])
    assert error.args == ("no median for empty data",)
assert raises(s.StatisticsError, s.mode, []).args == ("no mode for empty data",)
assert s.multimode([]) == []

for size in range(1, 36):
    source = []
    for i in range(size):
        source.append((i * 17 + size) % 29 - 14)
    before = source.copy()
    ordered = sorted(source)
    assert s.median_low(source) == ordered[(size - 1) // 2]
    assert s.median_high(source) == ordered[size // 2]
    expected = ordered[size // 2]
    if size % 2 == 0:
        expected = (ordered[size // 2 - 1] + expected) / 2
    assert s.median(source) == expected
    assert source == before

assert s.median([1, 2, 3, 4, 5, 6]) == 3.5
assert s.median([1, 2, 3, 4, 5, 6, 9]) == 4
assert s.median((1.5, 3.5)) == 2.5
assert s.median([True, False]) == 0.5
assert s.median([2**60, 2**60]) == 1.152921504606847e18
assert s.median([1.0, 2**60]) == 5.764607523034235e17
big = 10**400
for function in (s.median, s.median_low, s.median_high):
    assert function([big]) is big
# Exact integer addition precedes division: no premature float conversion.
assert s.median([10**400, -(10**400)]) == 0.0
raises(OverflowError, s.median, [10**400, 10**400])
assert s.median_low("dcab") == "b"
assert s.median_high("dcab") == "c"
assert s.median("cab") == "b"
raises(TypeError, s.median, ["a", "b"])
raises(TypeError, s.median, [1, "a"])
assert s.median([float("inf"), 1]) == float("inf")
assert math.isnan(s.median([float("inf"), -float("inf")]))
assert math.copysign(1.0, s.median([-0.0, -0.0])) == -1.0
assert s.median([1e308, 1e308]) == float("inf")

assert s.mode("abcbdb") == "b"
assert s.mode([1, 2, 2, 3, 6, 6]) == 2
assert s.mode(["a", "b", "b", "a"]) == "a"
assert s.multimode(["a", "b", "b", "a"]) == ["a", "b"]
assert s.multimode("aabbbbccddddeeffffgg") == ["b", "d", "f"]
assert s.multimode(["a", "b", "b", "c", "c", "c"]) == ["c"]
assert s.multimode([3, 1, 2]) == [3, 1, 2]
assert s.mode({"a": 1, "b": 99}) == "a"
assert s.multimode({"a": 1, "b": 99}) == ["a", "b"]
assert s.mode([True, 1, 1.0]) is True
assert s.multimode([True, 1, 1.0])[0] is True
assert s.mode([None, 1, None]) is None
for function in (s.mode, s.multimode):
    raises(TypeError, function, [[]])
    raises(TypeError, function, [{}])

nan = float("nan")
other_nan = float("nan")
assert s.mode([nan, nan, 1]) is nan
assert s.mode([nan, other_nan, 1]) is nan
result = s.multimode([nan, other_nan, 1])
assert len(result) == 3
assert result[0] is nan and result[1] is other_nan and result[2] == 1
assert s.multimode([nan, nan, other_nan])[0] is nan
for function in (s.median, s.median_low, s.median_high):
    assert function([nan]) is nan
assert math.isnan(s.median([nan, nan]))


class Ordered:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return self.value < other.value


low = Ordered(1)
middle = Ordered(2)
high = Ordered(3)
for function in (s.median, s.median_low, s.median_high):
    assert function([high, low, middle]) is middle
assert s.median_low([high, low]) is low
assert s.median_high([high, low]) is high


class SinglePass:
    def __init__(self, values):
        self.values = values
        self.index = 0
        self.iterations = 0

    def __iter__(self):
        self.iterations += 1
        assert self.iterations == 1
        return SinglePassIterator(self)


class SinglePassIterator:
    def __init__(self, source):
        self.source = source

    def __iter__(self):
        return self

    def __next__(self):
        source = self.source
        if source.index == len(source.values):
            raise StopIteration
        value = source.values[source.index]
        source.index += 1
        return value


for function in functions:
    source = SinglePass([3, 1, 2, 2])
    assert function(source) == function([3, 1, 2, 2])
    assert source.index == 4 and source.iterations == 1
    source = SinglePass([])
    if function is s.multimode:
        assert function(source) == []
    else:
        raises(s.StatisticsError, function, source)
    assert source.iterations == 1
