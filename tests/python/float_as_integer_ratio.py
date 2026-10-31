# Portions adapted from CPython Lib/test/test_float.py, v3.14.10.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.


class F(float):
    def __float__(self):
        raise AssertionError

    def __int__(self):
        raise AssertionError


class G(F):
    pass


cases = (
    (0.0, 0, 1), (-0.0, 0, 1), (0.875, 7, 8), (-0.875, -7, 8),
    (11.5, 23, 2), (2.5, 5, 2), (0.5, 1, 2), (-2100.0, -2100, 1),
    (2.1, 4728779608739021, 2251799813685248),
    (-2.1, -4728779608739021, 2251799813685248),
    (0.1, 3602879701896397, 36028797018963968),
    (1.0000000000000002, 4503599627370497, 4503599627370496),
    (0.9999999999999999, 9007199254740991, 9007199254740992),
    (5e-324, 1, 2**1074), (-5e-324, -1, 2**1074),
    (1e-323, 1, 2**1073), (1.5e-323, 3, 2**1074),
    (2.225073858507201e-308, 4503599627370495, 2**1074),
    (2.2250738585072014e-308, 1, 2**1022),
    (2.225073858507202e-308, 4503599627370497, 2**1074),
    (288230376151711744.0, 2**58, 1),
    (9223372036854775808.0, 2**63, 1),
    (1.7976931348623157e308, 2**1024 - 2**971, 1),
    (-1.7976931348623157e308, -(2**1024 - 2**971), 1),
)
retained = []
for cls in (float, F, G):
    for case in cases:
        value = cls(case[0])
        expected = (case[1], case[2])
        for repeat in range(3):
            pair = value.as_integer_ratio()
            assert pair == expected
            assert float.as_integer_ratio(value) == expected
            assert pair.__class__ is tuple
            assert pair[0].__class__ is int
            assert pair[1].__class__ is int
            assert pair[1] > 0
            retained.append(pair)
for cls in (float, F, G):
    for case in cases:
        for repeat in range(3):
            assert retained.pop(0) == (case[1], case[2])

class FloatLike:
    def __float__(self):
        raise AssertionError


for value in (None, 1, True, 2**100, "1", b"1", [], {}, FloatLike()):
    raised = False
    try:
        float.as_integer_ratio(value)
    except TypeError:
        raised = True
    assert raised

for cls in (float, F):
    for text in ("nan", "-nan"):
        raised = False
        try:
            cls(text).as_integer_ratio()
        except ValueError:
            raised = True
        assert raised
    for text in ("inf", "-inf"):
        raised = False
        try:
            cls(text).as_integer_ratio()
        except OverflowError:
            raised = True
        assert raised

raised = False
try:
    float.as_integer_ratio()
except TypeError:
    raised = True
assert raised
raised = False
try:
    float.as_integer_ratio(1.0, 2)
except TypeError:
    raised = True
assert raised
raised = False
try:
    (1.0).as_integer_ratio(2)
except TypeError:
    raised = True
assert raised
raised = False
try:
    float.as_integer_ratio(self=1.0)
except TypeError:
    raised = True
assert raised
raised = False
try:
    (1.0).as_integer_ratio(extra=1)
except TypeError:
    raised = True
assert raised


class Override(float):
    def as_integer_ratio(self):
        return (17, 19)


value = Override(0.5)
assert value.as_integer_ratio() == (17, 19)
assert float.as_integer_ratio(value) == (1, 2)


def ratio(value):
    return value.as_integer_ratio()


for repeat in range(10):
    assert ratio(value) == (17, 19)


def replacement(self):
    # Reenter the base implementation using this object's stored payload.
    return float.as_integer_ratio(self)


Override.as_integer_ratio = replacement
for repeat in range(10):
    assert ratio(value) == (1, 2)
del Override.as_integer_ratio
for repeat in range(10):
    assert ratio(value) == (1, 2)
assert (0.5).as_integer_ratio() == (1, 2)
