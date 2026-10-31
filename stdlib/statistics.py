"""A statistics subset using CloverVM's existing iterable and numeric protocols.

Median and quantile ordering delegate to sorted, including its intentional
divergence from CPython for NaNs and other inconsistently ordered values.
Counting inherits dictionary key support.
Decimal, Fraction and the remaining statistics APIs are outside this module's
current scope. Quantiles preserve exact integer intermediates and existing
binary64 arithmetic; weighted float operations can overflow or yield NaNs.
The number of intervals inherits the existing range/index conversion limits.
Geometric mean streams logarithms with compensated summation and inherits
platform log/exp accuracy and builtin numeric payload conversion limits.
"""

# Median algorithms adapted from CPython's Lib/statistics.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import math as _math
from math import exp as _exp, fsum as _fsum, log as _log

__all__ = (
    "StatisticsError", "median", "median_low", "median_high", "mode", "multimode",
    "quantiles", "geometric_mean"
)


class StatisticsError(ValueError):
    """An error in the supplied statistical data."""


# Geometric mean policy adapted from CPython Lib/statistics.py at v3.14.10.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
class _GeometricMeanLogs:
    """Stream logs while retaining the reference generator's validation order."""

    def __init__(self, data):
        try:
            self._source = iter(data)
        except StopIteration:
            # Acquiring the source occurs inside CPython's generator.
            raise RuntimeError("generator raised StopIteration")
        self.count = 0
        self.found_zero = False
        self._current = None

    def __iter__(self):
        return self

    def __next__(self):
        if self._source is None:
            raise StopIteration
        try:
            while True:
                # Keep the previous input alive during source reentry, as the
                # reference's suspended generator does. No input history grows.
                self._current = next(self._source)
                x = self._current
                self.count += 1
                try:
                    # Separate branches truth-test a comparison result once,
                    # without materializing a short-circuit expression value.
                    if x > 0.0:
                        positive = True
                    elif _math.isnan(x):
                        positive = True
                    else:
                        positive = False
                        if x == 0.0:
                            self.found_zero = True
                        else:
                            raise StatisticsError('No negative inputs allowed', x)
                except StopIteration:
                    raise RuntimeError("generator raised StopIteration")
                if positive:
                    # log belongs outside generator validation: in CPython it
                    # is the map callback, so its exceptions propagate directly.
                    return _log(x)
        except BaseException:
            self._source = None
            self._current = None
            raise


def geometric_mean(data):
    """Return a float geometric mean using compensated sums of logarithms.

    Empty and negative data raise StatisticsError. Zeros are allowed; NaNs and
    infinity follow CPython 3.14.10 policy. Accuracy inherits math.log/fsum/exp.
    """
    logs = _GeometricMeanLogs(data)
    total = _fsum(logs)
    if logs.count == 0:
        raise StatisticsError('Must have a non-empty dataset')
    if _math.isnan(total):
        return _math.nan
    if logs.found_zero:
        return _math.nan if total == _math.inf else 0.0
    return _exp(total / logs.count)


def median(data):
    """Return the middle value, averaging the two middle values for even data."""
    values = sorted(data)
    n = len(values)
    if n == 0:
        raise StatisticsError("no median for empty data")
    middle = n // 2
    if n % 2:
        return values[middle]
    return (values[middle - 1] + values[middle]) / 2


def median_low(data):
    """Return the original lower middle data point."""
    values = sorted(data)
    n = len(values)
    if n == 0:
        raise StatisticsError("no median for empty data")
    return values[(n - 1) // 2]


def median_high(data):
    """Return the original upper middle data point."""
    values = sorted(data)
    n = len(values)
    if n == 0:
        raise StatisticsError("no median for empty data")
    return values[n // 2]


def _counts(data):
    counts = {}
    for value in data:
        # One lookup per input, without rehashing to update the count. The dict
        # retains the first equal key; selection never hashes keys again.
        # Equality probe counts can differ from CPython's Counter accelerator.
        cell = counts.setdefault(value, [0])
        cell[0] += 1
    return counts


def mode(data):
    """Return the most frequent value, choosing the first encountered tie."""
    counts = _counts(data)
    if not counts:
        raise StatisticsError("no mode for empty data")
    best_count = 0
    for pair in counts.items():
        count = pair[1][0]
        if count > best_count:
            best = pair[0]
            best_count = count
    return best


def multimode(data):
    """Return all most frequent values in first-encounter order."""
    counts = _counts(data)
    result = []
    best_count = 0
    for pair in counts.items():
        count = pair[1][0]
        if count > best_count:
            result = [pair[0]]
            best_count = count
        elif count == best_count:
            result.append(pair[0])
    return result


# Quantile algorithm adapted from CPython Lib/statistics.py at v3.14.10.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
def quantiles(data, *, n=4, method='exclusive'):
    """Return n-1 cut points using exclusive or inclusive interpolation."""
    if n < 1:
        raise StatisticsError('n must be at least 1')

    data = sorted(data)

    ld = len(data)
    if ld < 2:
        if ld == 1:
            # Preserve references, without requiring list repetition.
            result = []
            for i in range(n - 1):
                result.append(data[0])
            return result
        raise StatisticsError('must have at least one data point')

    if method == 'inclusive':
        m = ld - 1
        result = []
        for i in range(1, n):
            # divmod is unavailable; both operations use exact builtin integers.
            j = i * m // n
            delta = i * m % n
            interpolated = (data[j] * (n - delta) + data[j + 1] * delta) / n
            result.append(interpolated)
        return result

    if method == 'exclusive':
        m = ld + 1
        result = []
        for i in range(1, n):
            j = i * m // n
            j = 1 if j < 1 else ld - 1 if j > ld - 1 else j
            delta = i * m - j * n  # Exact integer arithmetic after clamping.
            interpolated = (data[j - 1] * (n - delta) + data[j] * delta) / n
            result.append(interpolated)
        return result

    raise ValueError('Unknown method: ' + repr(method))
