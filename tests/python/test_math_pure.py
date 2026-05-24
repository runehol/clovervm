# Python test set -- math module
#
# Portions adapted from CPython's Lib/test/test_math.py
# at commit 0851700a9d99ca4bebd8d5f9d73c8c9ab1084405.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import math


def close(a, b):
    return math.isclose(a, b, 0.000000001, 0.000000001)


assert math.factorial(0) == 1
assert math.factorial(6) == 720
assert math.comb(5, 2) == 10
assert math.comb(5, 8) == 0
assert math.perm(5, 2) == 20
assert math.perm(5) == 120
assert math.gcd() == 0
assert math.gcd(24, 18, 30) == 6
assert math.lcm() == 1
assert math.lcm(6, 10, 15) == 30
assert math.isqrt(26) == 5

assert math.prod([2, 3, 4]) == 24
assert math.prod([2, 3], 5) == 30
assert close(math.fsum([1.0, 2.0, 3.0]), 6.0)
assert close(math.hypot(3.0, 4.0), 5.0)
assert close(math.dist([1.0, 2.0], [4.0, 6.0]), 5.0)
assert math.sumprod([1, 2, 3], [4, 5, 6]) == 32
assert close(math.sumprod([1.5, 2.5], [3.5, 4.5]), 16.5)

seen = False
try:
    math.sqrt(-1.0)
except ValueError:
    seen = True
assert seen

seen = False
try:
    math.log(0.0)
except ValueError:
    seen = True
assert seen

seen = False
try:
    math.exp(1000.0)
except OverflowError:
    seen = True
assert seen

seen = False
try:
    math.dist([1.0], [1.0, 2.0])
except ValueError:
    seen = True
assert seen

seen = False
try:
    math.nextafter(1.0, 2.0, -1)
except ValueError:
    seen = True
assert seen

seen = False
try:
    math.isqrt(-1)
except ValueError:
    seen = True
assert seen
