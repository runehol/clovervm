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
