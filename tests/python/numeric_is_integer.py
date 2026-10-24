# Numeric is_integer methods checked against CPython 3.14.
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

    def __int__(self):
        raise AssertionError


class G(F):
    pass


for value in (0, 1, -1, 2**58, -(2**58), 2**1000, -(2**1000)):
    for cls in (int, I, J):
        integer = cls(value)
        assert integer.is_integer() is True
        assert int.is_integer(integer) is True
for value in (True, False):
    assert value.is_integer() is True
    assert int.is_integer(value) is True

for case in (
    (0.0, True), (-0.0, True),
    (1.0, True), (-1.0, True),
    (1.5, False), (-1.5, False),
    (0.9999999999999999, False), (-0.9999999999999999, False),
    (5e-324, False), (-5e-324, False),
    (2.2250738585072014e-308, False),
    (4503599627370495.5, False),
    (4503599627370496.0, True),
    (9007199254740992.0, True),
    (1e100, True), (-1e100, True),
    (1.7976931348623157e308, True),
    (-1.7976931348623157e308, True),
    (math.inf, False), (-math.inf, False),
    (math.nan, False), (-math.nan, False),
):
    for cls in (float, F, G):
        value = cls(case[0])
        assert value.is_integer() is case[1]
        assert float.is_integer(value) is case[1]

class IntOverride(int):
    def is_integer(self):
        return "int override"


class FloatOverride(float):
    def is_integer(self):
        return "float override"


assert IntOverride(5).is_integer() == "int override"
assert int.is_integer(IntOverride(5)) is True
assert FloatOverride(1.5).is_integer() == "float override"
assert float.is_integer(FloatOverride(1.5)) is False

for value in (1.0, F(1.0), None, "1", b"1", [], {}):
    raised = False
    try:
        int.is_integer(value)
    except TypeError:
        raised = True
    assert raised
for value in (1, True, I(1), 2**100, None, "1", b"1", [], {}):
    raised = False
    try:
        float.is_integer(value)
    except TypeError:
        raised = True
    assert raised
for method in (int.is_integer, float.is_integer):
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
