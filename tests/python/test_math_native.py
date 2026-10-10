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

# A NaN input propagates through the shared native result check.
for func in (
    math.acos, math.acosh, math.asin, math.asinh, math.atan, math.atanh,
    math.cbrt, math.cos, math.cosh, math.erf, math.erfc, math.exp,
    math.exp2, math.expm1, math.fabs, math.gamma, math.lgamma, math.log,
    math.log10, math.log1p, math.log2, math.sin, math.sinh, math.sqrt,
    math.tan, math.tanh,
):
    assert math.isnan(func(math.nan))

for func in (math.atan2, math.fmod, math.pow, math.remainder):
    assert math.isnan(func(math.nan, 2.0))
    assert math.isnan(func(2.0, math.nan))
    assert math.isnan(func(math.nan, math.nan))

assert math.isnan(math.copysign(math.nan, 1.0))
assert math.isnan(math.ldexp(math.nan, 2))
assert math.isnan(math.fma(math.nan, 2.0, 3.0))
assert math.isnan(math.fma(1.0, math.nan, 3.0))
assert math.isnan(math.fma(1.0, 2.0, math.nan))

# Genuine domain errors still raise instead of returning NaN.
for case in (
    (math.sqrt, (-1.0,)),
    (math.sqrt, (-math.inf,)),
    (math.sin, (math.inf,)),
    (math.log, (0.0,)),
    (math.pow, (-1.0, 0.5)),
    (math.fmod, (1.0, 0.0)),
    (math.remainder, (1.0, 0.0)),
    (math.fma, (math.inf, 0.0, 1.0)),
):
    func = case[0]
    args = case[1]
    seen = False
    try:
        if len(args) == 1:
            func(args[0])
        elif len(args) == 2:
            func(args[0], args[1])
        else:
            func(args[0], args[1], args[2])
    except ValueError:
        seen = True
    assert seen

for case in (
    (math.exp, (1000.0,)),
    (math.pow, (1e308, 2.0)),
    (math.ldexp, (1.0, 1024)),
    (math.fma, (1e308, 2.0, 0.0)),
):
    func = case[0]
    args = case[1]
    seen = False
    try:
        if len(args) == 1:
            func(args[0])
        elif len(args) == 2:
            func(args[0], args[1])
        else:
            func(args[0], args[1], args[2])
    except OverflowError:
        seen = True
    assert seen
