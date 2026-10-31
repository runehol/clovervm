# Python test set -- math log builtin numeric payloads
#
# Portions adapted from CPython 3.14.10, Lib/test/test_math.py.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
import math


def raises(error, function, *args):
    try:
        function(*args)
    except error:
        return
    assert False


def close(actual, expected):
    assert abs(actual - expected) <= 2 * math.ulp(expected)


assert math.log(True) == 0.0
assert math.log(1) == 0.0
close(math.log(2), 0.6931471805599453)
close(math.log(10**20), 46.051701859880914)
close(math.log(10**400), 921.0340371976182)
close(math.log(10**1000), 2302.5850929940457)
close(math.log(10**400, 10**200), 2.0)
close(math.log(32, 2), 5.0)
assert math.log(math.inf) == math.inf
assert math.isnan(math.log(math.nan))

for value in (False, 0, -1, -(10**400), -0.0, -1.0, -math.inf):
    raises(ValueError, math.log, value)
    raises(ValueError, math.log, 2, value)
for value in (None, '2', b'2', [], object()):
    raises(TypeError, math.log, value)
    # Explicit base=None retains the existing wrapper convention, separately
    # asserted in test_math_payload_limits.py rather than the shared oracle.
    if value is not None:
        raises(TypeError, math.log, 2, value)
raises(ZeroDivisionError, math.log, 2, 1)
raises(ValueError, math.log, 0, None)
raises(ValueError, math.log, 0, 'bad base')
raises(TypeError, math.log, math.nan, 'bad base')
raises(TypeError, math.log)
raises(TypeError, math.log, 1, 2, 3)


class PayloadInt(int):
    def __int__(self):
        raise AssertionError

    def __float__(self):
        raise AssertionError

    def bit_length(self):
        raise AssertionError

    def __le__(self, other):
        raise AssertionError

    def __truediv__(self, other):
        raise AssertionError


class PayloadFloat(float):
    def __float__(self):
        raise AssertionError


for value in (1, 2, 10**20, 10**400):
    assert math.log(PayloadInt(value)) == math.log(value)
close(math.log(PayloadInt(10**400), PayloadInt(10**200)), 2.0)
for value in (0, -1, -(10**400)):
    raises(ValueError, math.log, PayloadInt(value))
assert math.log(PayloadFloat(2.0)) == math.log(2.0)

# Repeated mixed representations exercise existing dispatch caches.
for repeat in range(100):
    for value in (2, 10**20, 10**400, PayloadInt(2), PayloadInt(10**400)):
        assert math.log(value) > 0.0
