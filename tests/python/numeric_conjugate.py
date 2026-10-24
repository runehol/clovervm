import math


class I(int):
    def __int__(self):
        raise AssertionError

    def __index__(self):
        raise AssertionError


class J(I):
    pass


class F(float):
    def __float__(self):
        raise AssertionError


class G(F):
    pass


for value in (0, 1, -1, 2**58 - 1, -(2**58), 2**1000, -(2**1000)):
    for cls in (int, I, J):
        integer = cls(value)
        result = integer.conjugate()
        assert result == value
        assert result.__class__ is int
        assert int.conjugate(integer) == value
        if cls is int:
            assert result is integer
for value in (True, False):
    assert value.conjugate() == int(value)
    assert value.conjugate().__class__ is int

for value in (0.0, -0.0, 1.5, -1.5, 5e-324, -5e-324,
              1.7976931348623157e308, math.inf, -math.inf, math.nan, -math.nan):
    for cls in (float, F, G):
        number = cls(value)
        for result in (number.conjugate(), float.conjugate(number)):
            assert result.__class__ is float
            if math.isnan(value):
                assert math.isnan(result)
            else:
                assert result == value
            assert math.copysign(1.0, result) == math.copysign(1.0, value)
            if cls is float:
                assert result is number


class IntOverride(int):
    def conjugate(self):
        return "int override"


class FloatOverride(float):
    def conjugate(self):
        return "float override"


assert IntOverride(5).conjugate() == "int override"
assert int.conjugate(IntOverride(5)) == 5
assert FloatOverride(1.5).conjugate() == "float override"
assert float.conjugate(FloatOverride(1.5)) == 1.5

for value in (1.0, None, "1", b"1", [], {}):
    raised = False
    try:
        int.conjugate(value)
    except TypeError:
        raised = True
    assert raised
for value in (1, True, 2**100, None, "1", b"1", [], {}):
    raised = False
    try:
        float.conjugate(value)
    except TypeError:
        raised = True
    assert raised
for method in (int.conjugate, float.conjugate):
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
