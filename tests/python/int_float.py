# Float-to-int behavior checked against CPython 3.14.
import math


class I(int):
    pass


class J(I):
    pass


class F(float):
    pass


cases = (
    (0.0, "0"),
    (-0.0, "0"),
    (5e-324, "0"),
    (-5e-324, "0"),
    (0.9999999999999999, "0"),
    (-0.9999999999999999, "0"),
    (1.9999999999999998, "1"),
    (-1.9999999999999998, "-1"),
    (9007199254740992.0, "9007199254740992"),
    (2.882303761517117e+17, "288230376151711712"),
    (-2.8823037615171174e+17, "-288230376151711744"),
    (2.8823037615171174e+17, "288230376151711744"),
    (-2.882303761517118e+17, "-288230376151711808"),
    (9.223372036854775e+18, "9223372036854774784"),
    (-9.223372036854776e+18, "-9223372036854775808"),
    (9.223372036854776e+18, "9223372036854775808"),
    (-9.223372036854778e+18, "-9223372036854777856"),
    (1.8446744073709556e+19, "18446744073709555712"),
    (1e+20, "100000000000000000000"),
    (1e+100, "10000000000000000159028911097599180468360808563945281389781327557747838772170381060813469985856815104"),
    (-1e+100, "-10000000000000000159028911097599180468360808563945281389781327557747838772170381060813469985856815104"),
    (1.7976931348623157e+308, "179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368"),
    (-1.7976931348623157e+308, "-179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368"),
)

for cls in (int, I, J):
    for case in cases:
        for source in (case[0], F(case[0])):
            value = cls(source)
            assert value.__class__ is cls
            assert str(value) == case[1]
            direct = int.__new__(cls, source)
            assert direct.__class__ is cls
            assert str(direct) == case[1]
            method = float.__int__(source)
            assert method.__class__ is int
            assert str(method) == case[1]
            assert source.__int__() == method
            if cls is not int:
                value.extra = 42
                assert value.__dict__["extra"] == 42
    for source in (math.inf, -math.inf, F(math.inf), F(-math.inf)):
        raised = False
        try:
            cls(source)
        except OverflowError as error:
            assert str(error) == "cannot convert float infinity to integer"
            raised = True
        assert raised
    for source in (math.nan, -math.nan, F(math.nan)):
        raised = False
        try:
            cls(source)
        except ValueError as error:
            assert str(error) == "cannot convert float NaN to integer"
            raised = True
        assert raised

for source in (math.inf, -math.inf, F(math.inf)):
    raised = False
    try:
        float.__int__(source)
    except OverflowError as error:
        assert str(error) == "cannot convert float infinity to integer"
        raised = True
    assert raised
for source in (math.nan, F(math.nan)):
    raised = False
    try:
        float.__int__(source)
    except ValueError as error:
        assert str(error) == "cannot convert float NaN to integer"
        raised = True
    assert raised
for source in (1, True, None, "1", b"1", []):
    raised = False
    try:
        float.__int__(source)
    except TypeError:
        raised = True
    assert raised
