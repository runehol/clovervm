import math
import operator


for function in (abs, operator.abs, operator.__abs__):
    for value in (0, 1, -1, True, False, 10**30, -(10**30), -288230376151711744):
        result = function(value)
        expected = int(value)
        if expected < 0:
            expected = -expected
        assert result == expected
        assert result.__class__ is int
    positive_bigint = 10**30
    assert function(positive_bigint) is positive_bigint
    for value in (0.0, -0.0, 1.5, -1.5, math.inf, -math.inf):
        result = function(value)
        assert result.__class__ is float
        assert math.copysign(1.0, result) == 1.0
        assert result == math.fabs(value)
    assert math.isnan(function(math.nan))
    assert math.copysign(1.0, function(-math.nan)) == 1.0

assert int.__abs__(-3) == 3
assert int.__abs__(True).__class__ is int
assert float.__abs__(-1.5) == 1.5
for function in (int.__abs__, float.__abs__):
    raised = False
    try:
        function("invalid receiver")
    except TypeError:
        raised = True
    assert raised


class CustomAbs:
    def __abs__(self):
        return [42]


assert abs(CustomAbs())[0] == 42
assert operator.abs(CustomAbs())[0] == 42


class NotImplementedAbs:
    def __abs__(self):
        return NotImplemented


assert abs(NotImplementedAbs()) is NotImplemented


class RaisingAbs:
    def __abs__(self):
        raise ValueError


for function in (abs, operator.abs):
    raised = False
    try:
        function(RaisingAbs())
    except ValueError:
        raised = True
    assert raised


class MutableAbs:
    def __abs__(self):
        return 1


class InheritedAbs(MutableAbs):
    pass


def replacement_abs(self):
    return 2


item = InheritedAbs()
item.__abs__ = replacement_abs
assert abs(item) == 1
assert abs(item) == 1
MutableAbs.__abs__ = replacement_abs
assert abs(item) == 2
assert abs(item) == 2
del MutableAbs.__abs__
raised = False
try:
    abs(item)
except TypeError:
    raised = True
assert raised

for value in (None, "text", [1]):
    raised = False
    try:
        abs(value)
    except TypeError:
        raised = True
    assert raised

raised = False
try:
    abs()
except TypeError:
    raised = True
assert raised
raised = False
try:
    abs(1, 2)
except TypeError:
    raised = True
assert raised
