# Expected strings and call behavior checked against CPython 3.14.10.


class FloatSubclass(float):
    def __float__(self):
        raise AssertionError

    def __str__(self):
        raise AssertionError

    def __repr__(self):
        raise AssertionError


class DerivedFloat(FloatSubclass):
    pass


class OverrideHex(FloatSubclass):
    def hex(self):
        return "override"


class FloatLike:
    def __float__(self):
        raise AssertionError


cases = (
    (0.0, "0x0.0p+0"),
    (-0.0, "-0x0.0p+0"),
    (1.0, "0x1.0000000000000p+0"),
    (1.5, "0x1.8000000000000p+0"),
    (1.0000000000000002, "0x1.0000000000001p+0"),
    (0.9999999999999999, "0x1.fffffffffffffp-1"),
    (-0.1, "-0x1.999999999999ap-4"),
    (5e-324, "0x0.0000000000001p-1022"),
    (-5e-324, "-0x0.0000000000001p-1022"),
    (2.225073858507201e-308, "0x0.fffffffffffffp-1022"),
    (2.2250738585072014e-308, "0x1.0000000000000p-1022"),
    (2.225073858507202e-308, "0x1.0000000000001p-1022"),
    (1.7976931348623157e308, "0x1.fffffffffffffp+1023"),
    (-1.7976931348623157e308, "-0x1.fffffffffffffp+1023"),
    ("inf", "inf"),
    ("-inf", "-inf"),
    ("nan", "nan"),
    ("-nan", "nan"),
)

for cls in (float, FloatSubclass, DerivedFloat):
    for case in cases:
        value = cls(case[0])
        expected = case[1]
        result = value.hex()
        assert result == expected
        assert result.__class__ is str
        assert float.hex(value) == expected

value = OverrideHex(-0.0)
assert value.hex() == "override"
assert float.hex(value) == "-0x0.0p+0"
saved = DerivedFloat(1.5).hex
assert saved() == "0x1.8000000000000p+0"


def expect_type_error(method, *args, **kwargs):
    try:
        method(*args, **kwargs)
    except TypeError:
        return
    raise AssertionError


for invalid in (None, True, 1, "1.0", FloatLike()):
    expect_type_error(float.hex, invalid)
expect_type_error(float.hex)
expect_type_error(float.hex, self=1.0)
expect_type_error(float.hex, 1.0, 2)
expect_type_error((1.0).hex, 2)
expect_type_error((1.0).hex, extra=2)
