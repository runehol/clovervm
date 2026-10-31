# Python test set -- statistics.geometric_mean
# Portions adapted from CPython Lib/test/test_statistics.py at v3.14.10.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
import math
import statistics as s


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError('expected exception')


assert 'geometric_mean' in s.__all__
assert s.geometric_mean([4, 9]) == 6.0
assert s.geometric_mean(data=(4, 9)) == 6.0
assert s.geometric_mean(iter([4, 9])) == 6.0
assert isinstance(s.geometric_mean([1]), float)
assert s.geometric_mean([True]) == 1.0
assert s.geometric_mean([1, 1, 1]) == 1.0
# Independent product/root expectations and overflow/underflow-prone inputs.
# Products are never formed by the implementation.
assert s.geometric_mean([1.0715086071862673e301, 9.332636185032189e-302]) == 1.0
assert s.geometric_mean([1e308, 1e-308]) == 1.0
# Compensated log summation must preserve the small term between large opposite
# terms. The independent expected mean is the cube root of the input e value.
compensated = s.geometric_mean([1.0715086071862673e301, math.e,
                                9.332636185032189e-302])
assert abs(compensated - 1.3956124250860895) <= math.ulp(1.3956124250860895)
assert s.geometric_mean([5e-324]) == 5e-324
assert abs(s.geometric_mean([54, 24, 36]) - 36.0) <= 2 * math.ulp(36.0)
assert abs(s.geometric_mean([17.625]) - 17.625) <= 2 * math.ulp(17.625)
assert abs(s.geometric_mean([10**400, 1]) - 1e200) <= 256 * math.ulp(1e200)
# A one-ULP huge-int log discrepancy is amplified by exp. This explicit budget
# pins the inherited platform behavior; it is not a general accuracy guarantee.
assert abs(s.geometric_mean([10**1000, 1, 1, 1]) - 1e250) <= 1024 * math.ulp(1e250)

for data in ([0], [-0.0], [False], [4, -0.0, 9], [0, 10**1000]):
    result = s.geometric_mean(data)
    assert result == 0.0 and math.copysign(1.0, result) == 1.0
for data in ([math.nan], [math.nan, math.inf], [0, math.nan], [0, math.inf],
             [math.inf, -0.0], [math.nan, 0, math.inf]):
    assert math.isnan(s.geometric_mean(data))
assert s.geometric_mean([1, math.inf]) == math.inf
for data in ([], iter([])):
    assert raises(s.StatisticsError, s.geometric_mean, data).args == (
        'Must have a non-empty dataset',)
for x in (-1, -0.1, -math.inf, -10**20):
    for prefix in ([], [0], [math.nan], [math.inf]):
        assert raises(s.StatisticsError, s.geometric_mean, prefix + [x]).args == (
            'No negative inputs allowed', x)
for data in ([-10**400], [0, -10**400], [math.nan, -10**400], [10**1000]):
    raises(OverflowError, s.geometric_mean, data)
for data in (None, 42, [None], ['4'], [b'4'], [[]], [object()]):
    raises(TypeError, s.geometric_mean, data)
raises(TypeError, s.geometric_mean)
raises(TypeError, s.geometric_mean, [], [])
raises(TypeError, s.geometric_mean, [], data=[])
raises(TypeError, s.geometric_mean, other=[])


class PayloadInt(int):
    pass


class PayloadFloat(float):
    pass


assert s.geometric_mean([PayloadInt(4), PayloadFloat(9)]) == 6.0
assert s.geometric_mean([PayloadInt(0)]) == 0.0
assert math.isnan(s.geometric_mean([PayloadFloat(math.nan)]))
raises(OverflowError, s.geometric_mean, [PayloadInt(-10**400)])
negative = PayloadFloat(-2.0)
assert raises(s.StatisticsError, s.geometric_mean, [negative]).args[1] is negative
