# Bytes-to-float behavior checked against CPython 3.14.
import math


class F(float):
    pass


class G(F):
    pass


class B(bytes):
    def __bytes__(self):
        raise AssertionError

    def __str__(self):
        raise AssertionError


cases = (
    (b"1.25", 1.25),
    (b"+1.25", 1.25),
    (b"-.5", -0.5),
    (b"5.", 5.0),
    (b"00012.5", 12.5),
    (b" \t\n\r\v\f+1_000.2_5e-1 \t", 100.025),
    (b"1e1_0", 1e10),
    (b"4.9406564584124654e-324", 5e-324),
    (b"1.7976931348623157e308", 1.7976931348623157e308),
    (b"1.00000000000000011102230246251565404236316680908203125", 1.0),
    (b"1.00000000000000011102230246251565404236316680908203126", 1.0000000000000002),
    (b"1e9999", math.inf),
    (b" -1e9999 ", -math.inf),
    (b"-1e-9999", -0.0),
    (b"-0", -0.0),
    (b"-0.0", -0.0),
    (b" +INFINITY\t", math.inf),
)

for cls in (float, F, G):
    for case in cases:
        for source in (case[0], B(case[0])):
            result = cls(source)
            assert result.__class__ is cls
            assert result == case[1]
            assert math.copysign(1.0, result) == math.copysign(1.0, case[1])
            direct = float.__new__(cls, source)
            assert direct.__class__ is cls
            assert direct == result
            assert math.copysign(1.0, direct) == math.copysign(1.0, result)
            if cls is not float:
                result.extra = 42
                assert result.__dict__["extra"] == 42
    for text in (b"nan", b"+NaN", b"-NAN", b"\t-NaN "):
        for source in (text, B(text)):
            result = cls(source)
            assert result.__class__ is cls
            assert math.isnan(result)
            assert math.copysign(1.0, result) == (-1.0 if b"-" in text else 1.0)
    for text in (
        b"", b" \t\n", b"+", b"-", b".", b"1e", b"1e+", b"1.2x",
        b"1,000", b"1 000", b"0x1.8p+1", b"nan()", b"nan(payload)",
        b"-NaN(foo)", b"+nan(123)", b"infinite", b"_1", b"1_", b"1__0",
        b"1_.0", b"1._0", b"1e_2", b"1e+_2", b"na_n", b"in_f",
        b"1_ 2", b"1 _2", b"\x1c1.5", b"1.5\x1f", b" \tnan(foo) ",
        b"1.5\x00", b"1.5\x00junk", b"\xa01.5", b"1.5\x85",
        b"\xd9\xa1.5", b"\xc2\xa01.5", b"1.5\xe2\x80\x83", b"1_\xff",
    ):
        for source in (text, B(text)):
            raised = False
            try:
                cls(source)
            except ValueError:
                raised = True
            assert raised
