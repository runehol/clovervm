# String-to-float behavior checked against CPython 3.14.
import math


class F(float):
    pass


class G(F):
    pass


class S(str):
    def __str__(self):
        raise AssertionError


cases = (
    ("1.25", 1.25),
    ("+1.25", 1.25),
    ("-.5", -0.5),
    ("5.", 5.0),
    ("00012.5", 12.5),
    ("  +1_000.2_5e-1 \t\n", 100.025),
    ("1e1_0", 1e10),
    ("\u0661\u0662.\u0665", 12.5),
    ("1_\u0662", 12.0),
    ("\uff11.\uff12\uff15", 1.25),
    ("\U0001d7d9_\U0001d7da.5", 12.5),
    ("\U00011f51.5", 1.5),
    ("\u00a0-0\u2003", -0.0),
    ("\u0085+1.5\u3000", 1.5),
    ("4.9406564584124654e-324", 5e-324),
    ("1.7976931348623157e308", 1.7976931348623157e308),
    ("1.00000000000000011102230246251565404236316680908203125", 1.0),
    ("1.00000000000000011102230246251565404236316680908203126", 1.0000000000000002),
    ("1e9999", math.inf),
    (" -1e9999 ", -math.inf),
    ("-1e-9999", -0.0),
    ("\u00a0-INFINITY\u2003", -math.inf),
)

for cls in (float, F, G):
    for case in cases:
        for text in (case[0], S(case[0])):
            result = cls(text)
            assert result.__class__ is cls
            assert result == case[1]
            assert math.copysign(1.0, result) == math.copysign(1.0, case[1])
            direct = float.__new__(cls, text)
            assert direct.__class__ is cls
            assert direct == result
            if cls is not float:
                result.extra = 42
                assert result.__dict__["extra"] == 42
    for text in ("nan", "+NaN", "-NAN", "\u00a0-NaN "):
        result = cls(text)
        assert result.__class__ is cls
        assert math.isnan(result)
        assert math.copysign(1.0, result) == (-1.0 if "-" in text else 1.0)
    for text in (
        "", " \t\n", "+", "-", ".", "1e", "1e+", "1.2x", "1,000", "1 000",
        "0x1.8p+1", "nan()", "nan(payload)", "-NaN(foo)", "+nan(123)",
        "infinite", "\u22121.5", "_1", "1_", "1__0", "1_.0", "1._0", "1e_2",
        "1e+_2", "na_n", "in_f", "1_ 2", "1 _2", "1\u20032", "\x1c1.5",
        "1.5\x1f", "\u00b2.5", "1\u066b5", "\uff0b1.5", " \tnan(foo) ",
        "\u0661_", "_\u0661", "\u0661_\u2003\u0662", "1.5\x00", "1.5\x00junk",
    ):
        raised = False
        try:
            cls(text)
        except ValueError:
            raised = True
        assert raised
