# Python test set -- math module
#
# Portions adapted from CPython's Lib/test/test_math.py
# at commit 0851700a9d99ca4bebd8d5f9d73c8c9ab1084405.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import math


def close(a, b):
    return math.isclose(a, b, 0.000000001, 0.000000001)


assert close(math.pi, 3.141592653589793)
assert close(math.e, 2.718281828459045)
assert close(math.tau, 2.0 * math.pi)
assert math.isinf(math.inf)
assert math.isnan(math.nan)
assert not math.isfinite(math.inf)
assert not math.isfinite(math.nan)
assert math.isfinite(1.25)

assert close(math.acos(1.0), 0.0)
assert close(math.asin(1.0), math.pi / 2.0)
assert close(math.atan(1.0), math.pi / 4.0)
assert close(math.atan2(1.0, 1.0), math.pi / 4.0)
assert close(math.cos(0.0), 1.0)
assert close(math.sin(math.pi / 2.0), 1.0)
assert close(math.tan(0.0), 0.0)
assert close(math.cosh(0.0), 1.0)
assert close(math.sinh(0.0), 0.0)
assert close(math.tanh(0.0), 0.0)
assert close(math.acosh(1.0), 0.0)
assert close(math.asinh(0.0), 0.0)
assert close(math.atanh(0.0), 0.0)
assert close(math.cbrt(27.0), 3.0)
assert close(math.sqrt(9.0), 3.0)

assert math.ceil(1.25) == 2
assert math.ceil(-1.25) == -1
assert math.floor(1.25) == 1
assert math.floor(-1.25) == -2
assert math.trunc(1.75) == 1
assert math.trunc(-1.75) == -1

assert close(math.copysign(2.0, -1.0), -2.0)
assert close(math.degrees(math.pi), 180.0)
assert close(math.radians(180.0), math.pi)
assert close(math.exp(0.0), 1.0)
assert math.exp(math.inf) == math.inf
assert close(math.exp2(3.0), 8.0)
assert close(math.expm1(0.0), 0.0)
assert close(math.fabs(-3.5), 3.5)
assert close(math.fma(2.0, 3.0, 4.0), 10.0)
assert close(math.fmod(7.0, 4.0), 3.0)
assert close(math.remainder(7.0, 4.0), -1.0)
assert close(math.pow(2.0, 3.0), 8.0)
assert math.pow(math.inf, 2.0) == math.inf
