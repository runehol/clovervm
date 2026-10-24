import math


class F(float):
    def __float__(self):
        raise AssertionError

    def __int__(self):
        raise AssertionError


class G(F):
    pass


maximum_integer = 2**1024 - 2**971
for case in (
    (0.0, 0, 0, 0), (-0.0, 0, 0, 0),
    (1.0, 1, 1, 1), (-1.0, -1, -1, -1),
    (1.7, 1, 1, 2), (-1.7, -1, -2, -1),
    (0.9999999999999999, 0, 0, 1),
    (-0.9999999999999999, 0, -1, 0),
    (5e-324, 0, 0, 1), (-5e-324, 0, -1, 0),
    (4503599627370495.5, 4503599627370495, 4503599627370495, 4503599627370496),
    (288230376151711744.0, 2**58, 2**58, 2**58),
    (-288230376151711744.0, -(2**58), -(2**58), -(2**58)),
    (9223372036854775808.0, 2**63, 2**63, 2**63),
    (-9223372036854775808.0, -(2**63), -(2**63), -(2**63)),
    (1267650600228229401496703205376.0, 2**100, 2**100, 2**100),
    (1.7976931348623157e308, maximum_integer, maximum_integer, maximum_integer),
    (-1.7976931348623157e308, -maximum_integer, -maximum_integer, -maximum_integer),
):
    for cls in (float, F, G):
        value = cls(case[0])
        assert value.__trunc__() == case[1]
        assert value.__floor__() == case[2]
        assert value.__ceil__() == case[3]
        assert float.__trunc__(value) == case[1]
        assert float.__floor__(value) == case[2]
        assert float.__ceil__(value) == case[3]
        assert value.__trunc__().__class__ is int
        assert value.__floor__().__class__ is int
        assert value.__ceil__().__class__ is int

for method in (float.__trunc__, float.__floor__, float.__ceil__):
    for cls in (float, F):
        for value in (math.nan, -math.nan):
            raised = False
            try:
                method(cls(value))
            except ValueError:
                raised = True
            assert raised
        for value in (math.inf, -math.inf):
            raised = False
            try:
                method(cls(value))
            except OverflowError:
                raised = True
            assert raised
    for value in (None, 1, True, 2**100, "1", b"1", [], {}):
        raised = False
        try:
            method(value)
        except TypeError:
            raised = True
        assert raised
    raised = False
    try:
        method()
    except TypeError:
        raised = True
    assert raised
    raised = False
    try:
        method(1.0, 2)
    except TypeError:
        raised = True
    assert raised


class Override(float):
    def __trunc__(self):
        return 10

    def __floor__(self):
        return 20

    def __ceil__(self):
        return 30


value = Override(-1.7)
assert value.__trunc__() == 10
assert value.__floor__() == 20
assert value.__ceil__() == 30
assert float.__trunc__(value) == -1
assert float.__floor__(value) == -2
assert float.__ceil__(value) == -1
