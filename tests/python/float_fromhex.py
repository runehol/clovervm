import math


zeros = "0"
nines = "9"
for step in range(11):
    zeros = zeros + zeros
    nines = nines + nines


# Exact expectations from the binary64 spacing at each boundary.
for case in (
    ("\t\r\v\f +0X.8P+2 \n", (2, 1)),
    ("a.", (10, 1)),
    (".a", (5, 8)),
    ("12", (18, 1)),
    ("0x1.00000000000008p0", (1, 1)),
    ("0x1.000000000000080000000001p0", (2**52 + 1, 2**52)),
    ("0x1.00000000000018p0", (2**51 + 1, 2**51)),
    ("0x1.fffffffffffff8p0", (2, 1)),
    ("0x1.00000000000001p-1075", (1, 2**1074)),
    ("0x1.8p-1073", (3, 2**1074)),
    ("0x1.4p-1073", (1, 2**1073)),
    ("0x0.fffffffffffff7p-1022", (2**52 - 1, 2**1074)),
    ("0x0.fffffffffffff8p-1022", (1, 2**1022)),
    ("0x1.fffffffffffff7p1023", ((2**53 - 1) * 2**971, 1)),
    ("1" + zeros + "p-8192", (1, 1)),
    ("." + zeros + "1p8196", (1, 1)),
):
    result = float.fromhex(case[0])
    assert result.__class__ is float
    assert result.as_integer_ratio() == case[1]

for text in ("-0x0", "-0x1p-1075", "-1p-" + nines, "-0p+" + nines):
    result = float.fromhex(text)
    assert result == 0.0 and math.copysign(1.0, result) == -1.0
assert math.copysign(1.0, float.fromhex("1p-1075")) == 1.0
assert float.fromhex("  +iNf ") == math.inf
assert float.fromhex("-InFiNiTy") == -math.inf
for case in (("NaN", 1.0), ("-nAn", -1.0), ("+NaN", 1.0)):
    result = float.fromhex(case[0])
    assert math.isnan(result) and math.copysign(1.0, result) == case[1]


def expect_error(kind, fn, *args, **kwargs):
    try:
        fn(*args, **kwargs)
    except kind:
        return
    raise AssertionError


for text in ("", " ", ".", "+", "0x.", "0x", "1..0", "1p", "1p+",
             "1p1.0", "1p-1076junk", "1_0", "1,8", "+ 1", "1 0",
             "1\x00", "\x1c1", "\u00a01", "1\u2003", "\uff11", "1p\u0661",
             "infp0", "nan(payload)"):
    expect_error(ValueError, float.fromhex, text)
for text in ("1p1024", "0x1.fffffffffffff8p1023", "1p1024junk", "1p" + nines):
    expect_error(OverflowError, float.fromhex, text)

try:
    float.fromhex("1p1024\ud800\udfff")
    assert False
except UnicodeEncodeError as error:
    assert error.args == ("utf-8", "1p1024\ud800\udfff", 6, 8,
                          "surrogates not allowed")


class Text(str):
    def __str__(self):
        raise AssertionError


class Base(float):
    pass


class Derived(Base):
    def __new__(cls, value):
        assert value.__class__ is float
        return float.__new__(cls, value + 1.0)


class OtherResult(Base):
    def __new__(cls, value):
        return "constructor result"


class Failing(Base):
    def __new__(cls, value):
        raise RuntimeError


assert float.fromhex(Text("a")) == 10.0
assert float.fromhex(*("1p1",)) == 2.0
assert Base.fromhex("1p1").__class__ is Base
assert Base(0.0).fromhex("1p1").__class__ is Base
for cls in (float, Base, Derived, Base, float):
    assert cls.fromhex("1p1").__class__ is cls
result = Derived.fromhex("1p1")
assert result.__class__ is Derived and result == 3.0
assert Derived.fromhex(*("1p1",)) == 3.0
saved = Derived.fromhex
assert saved("1p1") == 3.0
assert OtherResult.fromhex("1p1") == "constructor result"
expect_error(RuntimeError, Failing.fromhex, "1p1")
expect_error(ValueError, Failing.fromhex, "0x")
for obj in (None, 1, 1.0, b"1", bytearray(b"1")):
    expect_error(TypeError, float.fromhex, obj)
expect_error(TypeError, float.fromhex)
expect_error(TypeError, float.fromhex, "1", "2")
expect_error(TypeError, float.fromhex, string="1")
try:
    Derived.fromhex(string="1")
    assert False
except TypeError:
    pass
