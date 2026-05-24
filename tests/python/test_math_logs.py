# Python test set -- math module
#
# Portions adapted from CPython's Lib/test/test_math.py
# at commit 0851700a9d99ca4bebd8d5f9d73c8c9ab1084405.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import math


def close(a, b):
    return math.isclose(a, b, 0.000000001, 0.000000001)


frexp_result = math.frexp(8.0)
assert close(frexp_result[0], 0.5)
assert frexp_result[1] == 4
assert close(math.ldexp(0.5, 4), 8.0)

modf_result = math.modf(-1.25)
assert close(modf_result[0], -0.25)
assert close(modf_result[1], -1.0)

assert close(math.log(math.e), 1.0)
assert close(math.log(8.0, 2.0), 3.0)
assert close(math.log10(100.0), 2.0)
assert close(math.log1p(0.0), 0.0)
assert close(math.log2(8.0), 3.0)

assert close(math.erf(0.0), 0.0)
assert close(math.erfc(0.0), 1.0)
assert close(math.gamma(5.0), 24.0)
assert close(math.lgamma(1.0), 0.0)

next_float = math.nextafter(1.0, math.inf)
assert next_float > 1.0
assert math.nextafter(1.0, math.inf, 0) == 1.0
assert math.nextafter(1.0, math.inf, 2) > next_float
assert close(math.ulp(1.0), next_float - 1.0)
