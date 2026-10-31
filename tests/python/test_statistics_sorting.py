# CloverVM's accepted sorting divergence for inconsistently ordered NaNs.
# CPython 3.14.5 returns NaN for each median of this three-element input.
# This slice delegates to existing sorted without filtering or special ordering.
import statistics as s

nan = float("nan")
values = [nan, 2, 1]
assert s.median(values) == 1
assert s.median_low(values) == 1
assert s.median_high(values) == 1
assert values[0] is nan and values[1] == 2 and values[2] == 1

# Quantiles inherit the same ordering; CPython gives NaNs at all three cuts.
import math

for method in ('exclusive', 'inclusive'):
    result = s.quantiles(values, method=method)
    assert math.isnan(result[0])
    assert result[1] == 1.0
    if method == 'exclusive':
        assert result[2] == 2.0
    else:
        assert result[2] == 1.5
assert values[0] is nan and values[1] == 2 and values[2] == 1
