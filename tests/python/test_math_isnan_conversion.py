import math


def raises(error, function, *args):
    try:
        function(*args)
    except error:
        return
    assert False


class PayloadInt(int):
    def __ne__(self, other):
        raise AssertionError


class PayloadFloat(float):
    def __ne__(self, other):
        raise AssertionError


class ComparisonTrap:
    def __ne__(self, other):
        raise AssertionError


for value in (True, False, 0, 10**20, -10**20, 0.0, -0.0,
              math.inf, -math.inf, PayloadInt(2), PayloadFloat(2.0)):
    assert math.isnan(value) is False
for value in (math.nan, PayloadFloat(math.nan)):
    assert math.isnan(value) is True
for value in (10**400, -(10**400), PayloadInt(10**400)):
    raises(OverflowError, math.isnan, value)
for value in (None, 'nan', b'nan', [], object(), ComparisonTrap()):
    raises(TypeError, math.isnan, value)
raises(TypeError, math.isnan)
raises(TypeError, math.isnan, 1, 2)

# Existing managed consumers receive converted floats on their normal paths.
assert math.fsum([True, 10**20, -10**20, 2]) == 3.0
assert math.isnan(math.fsum([math.nan]))
assert math.hypot(3, 4) == 5.0
assert math.dist([0, 0], [3, 4]) == 5.0
assert math.isfinite(10**20)
assert not math.isfinite(math.nan)
assert math.isclose(10**20, 1e20)

for repeat in range(100):
    assert math.isnan(PayloadFloat(math.nan))
    assert not math.isnan(PayloadInt(10**20))
    raises(OverflowError, math.isnan, -(10**400))
