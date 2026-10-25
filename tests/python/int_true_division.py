import math


def divide(left, right):
    return left / right


def direct(left, right):
    return int.__truediv__(left, right)


def reflected(left, right):
    return int.__rtruediv__(right, left)


def check_result(actual, expected):
    assert actual == expected
    if expected == 0.0:
        assert math.copysign(1.0, actual) == math.copysign(1.0, expected)


# Independently evaluated with CPython 3.14.5. The powers keep the large
# operands readable; expected binary64 values are stored rather than recomputed.
# Cases cover rounding ties, lost shift bits, subnormal transitions and carries.
# Keep fixture construction within the current per-function cache limit.
def ratio_cases_0():
    return [
        (7, 2, 3.5),
        (2**53, 3, 3002399751580330.5),
        (2**53+1, 3, 3002399751580331.0),
        (2**53+3, 2**53, 1.0000000000000004),
        (148584462635267220, 42107047668565897, 3.5287314324387964),
        (2**60+2**60, 2, 1.152921504606847e+18),
        (10**400, 10**400, 1.0),
        (10**400, 10**399, 10.0),
        (0, -(2**60), -0.0),
        (1, 2**1074, 5e-324),
        (1, 2**1075, 0.0),
        (-1, 2**1075, -0.0),
        (3, 2**1076, 5e-324),
        (3, 2**1075, 1e-323),
        (2**52-1, 2**1074, 2.225073858507201e-308),
        (2**53-1, 2**1075, 2.2250738585072014e-308),
    ]


def ratio_cases_1():
    return [
        (2**53+1, 2**1075, 2.2250738585072014e-308),
        ((2**53-1)*2**971, 1, 1.7976931348623157e+308),
        ((2**54-1)*2**970-1, 1, 1.7976931348623157e+308),
        ((2**54+2)*2**0-1, 2**54, 1.0),
        ((2**54+2)*2**0+0, 2**54, 1.0),
        ((2**54+2)*2**0+1, 2**54, 1.0000000000000002),
        ((2**54+2)*2**58-1, 2**112, 1.0),
        ((2**54+2)*2**58+0, 2**112, 1.0),
        ((2**54+2)*2**58+1, 2**112, 1.0000000000000002),
        ((2**54+2)*2**1000-1, 2**1054, 1.0),
        ((2**54+2)*2**1000+0, 2**1054, 1.0),
        ((2**54+2)*2**1000+1, 2**1054, 1.0000000000000002),
        ((2**54+2)*2**4000-1, 2**4054, 1.0),
        ((2**54+2)*2**4000+0, 2**4054, 1.0),
        ((2**54+2)*2**4000+1, 2**4054, 1.0000000000000002),
        (2**54-1, 2**54+3, 0.9999999999999998),
    ]


def ratio_cases_2():
    return [
        (2**54+1, 2**54+3, 0.9999999999999999),
        (2**58-1, 2**58+3, 1.0),
        (2**58+1, 2**58+3, 1.0),
        (2**64-1, 2**64+3, 1.0),
        (2**64+1, 2**64+3, 1.0),
        (2**1023-1, 2**1023+3, 1.0),
        (2**1023+1, 2**1023+3, 1.0),
        (2**1074-1, 2**1074+3, 1.0),
        (2**1074+1, 2**1074+3, 1.0),
        (2**2000-1, 2**2000+3, 1.0),
        (2**2000+1, 2**2000+3, 1.0),
        (2**54-18, -(2**1+15), -1059670500557762.8),
        (-(2**59+87), 2**60+9, -0.5000000000000001),
        (2**500-86, 2**1574+28, 5e-324),
        (-(2**57-64), -(2**1079+86), 2.2250738585072004e-308),
        (2**500+98, 2**1574+53, 5e-324),
    ]


def ratio_cases_3():
    return [
        (-(2**58-56), 2**112+4, -5.5511151231257815e-17),
        (2**4000+85, -(2**5074+13), -5e-324),
        (-(2**53+93), 2**53+12, -1.0000000000000089),
        (2**64-74, 2**1086+24, 2.2250738585072014e-308),
        (-(2**4000+39), -(2**3946+6), 1.8014398509481984e+16),
        (2**4000-66, 2**4001+98, 0.5),
        (-(2**500-48), 2**1574+65, -5e-324),
        (2**57-49, -(2**1131+1), -5e-324),
        (-(2**4000-56), 2**3999+94, -2.0),
        (2**53+100, 2**1+61, 142971416741922.1),
        (-(2**53-69), -(2**1+71), 123386291160834.56),
        (2**500+5, 2**501+22, 0.5),
        (-(2**64-37), 2**1138+50, -5e-324),
        (2**500-72, -(2**446+9), -1.8014398509481984e+16),
        (-(2**1023+94), 2**1023+95, -1.0),
        (2**65+17, 2**119+33, 5.551115123125783e-17),
    ]


def ratio_cases_4():
    return [
        (-(2**59-91), -(2**59+100), 0.9999999999999997),
        (2**500-42, 2**1574+11, 5e-324),
        (-(2**58+66), 2**1080+5, -2.225073858507202e-308),
        (2**64-95, -(2**1138+78), -5e-324),
        (-(2**57-98), 2**3+86, -1533140298679316.8),
        (2**1023+42, 2**1023+26, 1.0),
        (-(2**1074-78), -(2**2148+66), 5e-324),
        (2**54+96, 2**108+83, 5.551115123125812e-17),
        (-(2**57-57), 2**1131+72, -5e-324),
        (2**53+43, -(2**107+40), -5.551115123125809e-17),
        (-(2**65-90), 2**11+26, -1.778856709133033e+16),
        (2**57-79, 2**56+54, 1.9999999999999973),
        (-(2**1023+17), -(2**1022+79), 2.0),
        (2**65+53, 2**64+24, 2.0),
        (-(2**1023-36), 2**2097+99, -5e-324),
        (2**500+25, -(2**500+93), -1.0),
    ]


def ratio_cases_5():
    return [
        (-(2**1023-88), 2**1023+14, -1.0),
        (2**59+77, 2**59+68, 1.0),
        (-(2**500-64), -(2**499+83), 2.0),
        (2**58+23, 2**1080+86, 2.2250738585072014e-308),
        (-(2**59-21), 2**113+41, -5.551115123125783e-17),
        (2**500-39, -(2**501+74), -0.5),
        (-(2**65-14), 2**65+76, -1.0),
        (2**64+10, 2**65+60, 0.5),
        (-(2**1023-57), -(2**1023+60), 1.0),
        (2**59-18, 2**5+93, 4611686018427388.0),
        (-(2**54+71), 2**1076+0, -2.2250738585072103e-308),
    ]


cases = ratio_cases_0() + ratio_cases_1() + ratio_cases_2() + ratio_cases_3() + ratio_cases_4() + ratio_cases_5()

# Each method has one reusable call site. Repetition and alternating shapes
# exercise cold dispatch, trusted replay and cache replacement.
for case in cases:
    for method in [divide, direct, reflected]:
        for repeat in range(3):
            check_result(method(case[0], case[1]), case[2])
            if case[0] != 0:
                check_result(method(-case[0], case[1]), -case[2])
            check_result(method(case[0], -case[1]), -case[2])

# SMI/bool and inherited integer-subclass arithmetic, including small heap
# subclass values, must not invoke conversion hooks.
class I(int):
    def __float__(self):
        raise RuntimeError("unexpected __float__")
    def __int__(self):
        raise RuntimeError("unexpected __int__")

for left in [6, True, 2**60, I(6), I(2**60)]:
    for right in [3, True, 2**59, I(3), I(2**59)]:
        for repeat in range(3):
            check_result(divide(left, right), direct(left, right))
            check_result(reflected(left, right), direct(left, right))

class R(int):
    def __rtruediv__(self, other):
        return 91
class L(int):
    def __truediv__(self, other):
        return 92
class NI(int):
    def __rtruediv__(self, other):
        return NotImplemented

for repeat in range(3):
    assert divide(2**60, R(2)) == 91
    assert divide(L(2**60), 2) == 92
    assert direct(L(2**60), 2) == 2**59
    assert divide(3, NI(2)) == 1.5
    assert divide(2**60, NI(2)) == 2**59

def replacement(self, other):
    return 93
R.__rtruediv__ = replacement
for repeat in range(3):
    assert divide(2**60, R(2)) == 93

# Direct invalid receivers still raise; valid receivers return NotImplemented
# for unsupported operands. Merely defining __index__ is not integer division.
class IndexOnly:
    def __index__(self):
        raise RuntimeError("unexpected __index__")
for method in [int.__truediv__, int.__rtruediv__]:
    for receiver in [3, True, 2**60, I(3)]:
        assert method(receiver, 1.0) is NotImplemented
        assert method(receiver, "x") is NotImplemented
        assert method(receiver, IndexOnly()) is NotImplemented
    for receiver in [1.0, "x", IndexOnly()]:
        try:
            method(receiver, 2**60)
            assert False
        except TypeError:
            pass

for method in [divide, direct, reflected]:
    for denominator in [0, False, I(0)]:
        for numerator in [0, True, 3, 2**60, I(3)]:
            for repeat in range(2):
                try:
                    method(numerator, denominator)
                    assert False
                except ZeroDivisionError as error:
                    assert str(error) == "division by zero"
    for numerator in [2**1024, (2**54-1)*2**970, -(10**400)]:
        for repeat in range(2):
            try:
                method(numerator, 1)
                assert False
            except OverflowError as error:
                assert str(error) == "integer division result too large for a float"

# Preserve the existing mixed-float division repair.
for repeat in range(3):
    assert divide(2**60, 2.0) == 2**59
    assert divide(2.0, 2**60) == 1.734723475976807e-18
