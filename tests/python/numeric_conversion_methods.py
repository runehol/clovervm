import math


class I(int):
    pass


class J(I):
    pass


for value in (0, 1, -1, 2**58 - 1, -(2**58), 2**58, 2**1000, -(2**1000)):
    for cls in (int, I, J):
        integer = cls(value)
        for result in (integer.__int__(), integer.__index__(),
                       int.__int__(integer), int.__index__(integer)):
            assert result == value
            assert result.__class__ is int
            if cls is int:
                assert result is integer
for value in (True, False):
    for result in (value.__int__(), value.__index__()):
        assert result == int(value)
        assert result.__class__ is int


class IntOverride(int):
    def __int__(self):
        return 10

    def __index__(self):
        return 20


value = IntOverride(2**100)
assert value.__int__() == 10
assert value.__index__() == 20
assert int.__int__(value) == 2**100
assert int.__index__(value) == 2**100
assert int.__int__(value).__class__ is int
assert int.__index__(value).__class__ is int


class F(float):
    pass


class G(F):
    pass


for value in (0.0, -0.0, 1.5, -1.5, 5e-324, -5e-324,
              1.7976931348623157e308, math.inf, -math.inf, math.nan, -math.nan):
    for cls in (float, F, G):
        number = cls(value)
        for result in (number.__float__(), float.__float__(number)):
            assert result.__class__ is float
            if math.isnan(value):
                assert math.isnan(result)
            else:
                assert result == value
            assert math.copysign(1.0, result) == math.copysign(1.0, value)
            if cls is float:
                assert result is number


class FloatOverride(float):
    def __float__(self):
        return 10.0


value = FloatOverride(-1.5)
assert value.__float__() == 10.0
assert float.__float__(value) == -1.5
assert float.__float__(value).__class__ is float

for method in (int.__int__, int.__index__):
    for value in (1.0, None, "1", b"1", [], {}):
        raised = False
        try:
            method(value)
        except TypeError:
            raised = True
        assert raised
for value in (1, True, 2**100, None, "1", b"1", [], {}):
    raised = False
    try:
        float.__float__(value)
    except TypeError:
        raised = True
    assert raised
for method in (int.__int__, int.__index__, float.__float__):
    raised = False
    try:
        method()
    except TypeError:
        raised = True
    assert raised
    raised = False
    try:
        method(1, 2)
    except TypeError:
        raised = True
    assert raised
