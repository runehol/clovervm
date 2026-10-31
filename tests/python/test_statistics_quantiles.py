# Python test set -- statistics.quantiles
#
# Portions adapted from CPython's Lib/test/test_statistics.py at v3.14.10.
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


assert 'quantiles' in s.__all__
assert s.quantiles(data=[1, 2]) == [0.75, 1.5, 2.25]
raises(TypeError, s.quantiles)
raises(TypeError, s.quantiles, [1, 2], 4)
raises(TypeError, s.quantiles, [1, 2], method='inclusive', extra=1)
raises(TypeError, s.quantiles, [1, 2], data=[1, 2])
for n in (0, -1, -10**400, False, 0.5):
    assert raises(s.StatisticsError, s.quantiles, None, n=n).args == (
        'n must be at least 1',)
for n in (1, 4, 10**400):
    assert raises(s.StatisticsError, s.quantiles, [], n=n, method='bad').args == (
        'must have at least one data point',)
for n in (1.0, 1.5, float('nan'), float('inf')):
    raises(TypeError, s.quantiles, [1], n=n)
    raises(TypeError, s.quantiles, [1, 2], n=n)
    # Empty input and unknown methods precede integer conversion.
    raises(s.StatisticsError, s.quantiles, [], n=n)
    raises(ValueError, s.quantiles, [1, 2], n=n, method='bad')
for n in (None, '4'):
    raises(TypeError, s.quantiles, None, n=n)
raises(ValueError, s.quantiles, [1, 2], n=10**400, method='bad')
for method in ('bad', None, 3):
    error = raises(ValueError, s.quantiles, [1, 2], n=1, method=method)
    assert error.args == ('Unknown method: ' + repr(method),)

class Intervals(int):
    def __index__(self):
        raise AssertionError('stored integer payload must bypass index override')


for method in ('exclusive', 'inclusive'):
    assert s.quantiles([0, 10], n=Intervals(4), method=method) == s.quantiles(
        [0, 10], n=4, method=method)
    assert s.quantiles([7], n=Intervals(4), method=method) == [7, 7, 7]

# CPython's hand-computed fixtures include extrapolation for small samples.
source = [350, 120, 320, 200, 250]
before = source.copy()
for pair in (
    (1, []), (2, [250.0]), (3, [200.0, 320.0]),
    (4, [160.0, 250.0, 335.0]),
    (8, [100.0, 160.0, 212.5, 250.0, 302.5, 335.0, 357.5]),
):
    assert s.quantiles(source, n=pair[0]) == pair[1]
assert source == before
assert s.quantiles([800, 100, 400, 200], method='inclusive') == [175., 300., 500.]
assert s.quantiles([0, 100], n=10, method='inclusive') == [
    10., 20., 30., 40., 50., 60., 70., 80., 90.]
assert s.quantiles([0, 10]) == [-2.5, 5., 12.5]
for method in ('inclusive', 'exclusive'):
    assert s.quantiles([1, 2], n=1, method=method) == []
    assert s.quantiles([1, 2], n=True, method=method) == []
    for size in range(2, 20):
        data = []
        for i in range(size):
            data.append((i * 17) % 23)
        assert s.quantiles(data, method=method)[1] == s.median(data)

# Singleton results preserve type and identity, without inspecting method.
marker = object()
for method in ('exclusive', 'inclusive', 'bad', None):
    assert s.quantiles([marker], n=1, method=method) == []
    result = s.quantiles([marker], method=method)
    assert len(result) == 3
    assert result[0] is marker and result[1] is marker and result[2] is marker
    big = 10**400
    result = s.quantiles([big], method=method)
    assert result[0] is big and result[2] is big

# Independently derived integer fixtures: exact cancellation precedes conversion,
# and binary64 rounding uses ties-to-even at and above 2**53.
for method in ('exclusive', 'inclusive'):
    assert s.quantiles([-10**400, 10**400], n=2, method=method) == [0.0]
    assert s.quantiles([2**53, 2**53 + 2], method=method) == [
        9007199254740992.0, 9007199254740992.0, 9007199254740994.0]
    raises(OverflowError, s.quantiles, [10**400, 10**400], method=method)
    # Weighted floating arithmetic can overflow even when the exact answer fits.
    assert s.quantiles([1e308, 1e308], method=method) == [
        float('inf'), float('inf'), float('inf')]
    assert s.quantiles([5e-324, 5e-324], method=method) == [5e-324, 5e-324, 5e-324]
    assert s.quantiles([1, 2.0], n=2, method=method) == [1.5]
    assert s.quantiles([False, True], n=2, method=method) == [0.5]
    result = s.quantiles([-float('inf'), float('inf')], method=method)
    assert math.isnan(result[1])
    if method == 'inclusive':
        assert math.isnan(result[0]) and math.isnan(result[2])
    else:
        assert result[0] == -float('inf') and result[2] == float('inf')
result = s.quantiles([float('inf'), float('inf')])
assert math.isnan(result[0]) and result[1] == float('inf') and math.isnan(result[2])
assert s.quantiles([float('inf'), float('inf')], method='inclusive') == [
    float('inf'), float('inf'), float('inf')]
for pair in (('exclusive', [1., -1., 1.]), ('inclusive', [-1., -1., -1.])):
    method = pair[0]
    signs = pair[1]
    result = s.quantiles([-0., -0.], method=method)
    for i in range(3):
        assert result[i] == 0.0 and math.copysign(1., result[i]) == signs[i]
raises(TypeError, s.quantiles, [1, 'a'])
raises(TypeError, s.quantiles, [None, None])
