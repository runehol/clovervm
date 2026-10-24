# Shared comparison sites exercise cold dispatch and warmed trusted replay.
def eq(left, right):
    return left == right


def ne(left, right):
    return left != right


def check(number, integer, expected):
    for repeat in range(4):
        assert eq(number, integer) is expected
        assert eq(integer, number) is expected
        assert ne(number, integer) is (not expected)
        assert ne(integer, number) is (not expected)
        assert float.__eq__(number, integer) is expected
        assert float.__ne__(number, integer) is (not expected)
        assert int.__eq__(integer, number) is NotImplemented
        assert int.__ne__(integer, number) is NotImplemented
        assert [number] == [integer] if expected else [number] != [integer]
        assert (number,) == (integer,) if expected else (number,) != (integer,)
        assert [[number]] == [[integer]] if expected else [[number]] != [[integer]]


for exponent in (0, 1, 31, 32, 52, 53, 54, 57, 58, 59, 60, 63, 64, 100, 511, 1023):
    integer = 2**exponent
    number = float(integer)
    for sign in (1, -1):
        check(number * sign, integer * sign, True)
        check(number * sign, (integer - 1) * sign, False)
        check(number * sign, (integer + 1) * sign, False)
        check(number * sign, -integer * sign, False)
# Maximal significand and the top finite binary64 exercise every digit edge.
for integer in (2**53 - 1, 2**53 + 2, 2**58 - 32, 2**58 + 64,
                (2**53 - 1) * 2**971):
    for sign in (1, -1):
        check(float(integer) * sign, integer * sign, True)
        check(float(integer) * sign, (integer + 1) * sign, False)
        check(float(integer) * sign, (integer - 1) * sign, False)
check(9007199254740992.0, 9007199254740993, False)
check(1152921504606846976.0, 2**60, True)
for number in (0.0, -0.0):
    check(number, 0, True)
    check(number, False, True)
    check(number, True, False)
check(1.0, True, True)
check(-1.0, True, False)
for number in (0.5, -0.5, 1.5, -1.5, 5e-324, -5e-324, 2.2250738585072014e-308):
    for integer in (0, 1, -1, 2**100):
        check(number, integer, False)
for number in (float('inf'), -float('inf'), float('nan')):
    for integer in (0, 1, -1, 2**2000, -(2**2000)):
        check(number, integer, False)
check(1.0, 2**2000, False)
check(-1.0, -(2**2000), False)
for number in (1.0, float('inf'), -float('inf')):
    assert eq(number, number)
    assert not ne(number, number)
assert not eq(float('nan'), float('nan'))
assert ne(float('nan'), float('nan'))

for other in (None, '1', [], object()):
    assert float.__eq__(1.0, other) is NotImplemented
    assert float.__ne__(1.0, other) is NotImplemented
for method in (float.__eq__, float.__ne__):
    for receiver in (0, True, None, '1'):
        try:
            method(receiver, 1.0)
        except TypeError:
            pass
        else:
            assert False


class PlainI(int):
    pass


class PlainF(float):
    pass


for case in ((2**53 + 1, 9007199254740992.0, False),
             (2**60, 1152921504606846976.0, True), (0, -0.0, True)):
    integer = case[0]
    number = case[1]
    expected = case[2]
    check(number, PlainI(integer), expected)
    check(PlainF(number), integer, expected)
    check(PlainF(number), PlainI(integer), expected)


# Intentional difference from CPython 3.14.5: its large-int comparison path
# can invoke these overrides internally, depending on sign and magnitude.
# Clover's builtin float methods consistently compare the integer payload.
# Ordinary dispatch still invokes the overrides when the int subclass is left.
class I(int):
    def __float__(self):
        raise AssertionError('must use the integer payload')
    def __int__(self):
        raise AssertionError('must use the integer payload')
    def __eq__(self, other):
        return 'int eq'
    def __ne__(self, other):
        return 'int ne'


class F(float):
    pass


for integer in (0, 1, 2**53 + 1, 2**60):
    value = I(integer)
    number = float(integer)
    expected = integer != 2**53 + 1
    assert float.__eq__(number, value) is expected
    assert float.__ne__(number, value) is (not expected)
    assert eq(number, value) is expected
    assert ne(number, value) is (not expected)
    assert eq(value, number) == 'int eq'
    assert ne(value, number) == 'int ne'

value = F(2**60)
for repeat in range(4):
    assert eq(value, 2**60)
    assert eq(2**60, value)
    assert not ne(value, 2**60)
    assert not ne(2**60, value)


def replacement_eq(self, other):
    return 'float eq'


def replacement_ne(self, other):
    return 'float ne'


F.__eq__ = replacement_eq
F.__ne__ = replacement_ne
for repeat in range(4):
    assert eq(value, 2**60) == 'float eq'
    assert eq(2**60, value) == 'float eq'
    assert eq(1.0, value) == 'float eq'  # strict float subtype priority
    assert ne(value, 2**60) == 'float ne'
    assert ne(2**60, value) == 'float ne'
    assert ne(1.0, value) == 'float ne'
assert float.__eq__(value, 2**60) is True
assert float.__ne__(value, 2**60) is False
del F.__eq__
del F.__ne__
for repeat in range(4):
    assert eq(value, 2**60)
    assert eq(2**60, value)
    assert not ne(value, 2**60)
    assert not ne(2**60, value)

I.__eq__ = replacement_eq
I.__ne__ = replacement_ne
assert eq(I(1), 1.0) == 'float eq'
assert ne(I(1), 1.0) == 'float ne'
del I.__eq__
del I.__ne__
assert eq(I(2**60), float(2**60))
assert not ne(I(2**60), float(2**60))
