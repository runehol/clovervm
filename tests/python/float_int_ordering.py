# Shared comparison sites exercise cold dispatch and warmed trusted replay.
def order(a, b):
    return (a < b, a <= b, a > b, a >= b)


# These sites must stay separate: alternating shapes at one site evicts the
# cache entry and would let mutation tests pass without testing invalidation.
def reflected_order(a, b):
    return (a < b, a <= b, a > b, a >= b)


def subtype_order(a, b):
    return (a < b, a <= b, a > b, a >= b)


def expected(comparison):
    return (comparison < 0, comparison <= 0, comparison > 0, comparison >= 0)


def check(number, integer, comparison):
    forward = expected(comparison)
    reverse = expected(-comparison)
    for repeat in range(4):
        assert order(number, integer) == forward
        assert order(integer, number) == reverse
        assert (float.__lt__(number, integer), float.__le__(number, integer),
                float.__gt__(number, integer), float.__ge__(number, integer)) == forward
        assert int.__lt__(integer, number) is NotImplemented
        assert int.__le__(integer, number) is NotImplemented
        assert int.__gt__(integer, number) is NotImplemented
        assert int.__ge__(integer, number) is NotImplemented
        assert order([number], [integer]) == forward
        assert order((number,), (integer,)) == forward
        assert order([[number]], [[integer]]) == forward


def test_numeric_boundaries():
    for exponent in (0, 1, 31, 32, 48, 52, 53, 54, 57, 58, 59, 60, 63, 64, 100, 511, 1023):
        base = 2**exponent
        for sign in (1, -1):
            for offset in (-1, 0, 1):
                check(float(base) * sign, (base + offset) * sign, -offset * sign)
    # Maximal significands and a shift crossing each base-2**32 digit boundary.
    for base in (2**53 - 1, 2**53 + 2, 2**58 - 32, 2**58 + 64,
                 (2**53 - 1) * 2**971):
        for sign in (1, -1):
            for offset in (-1, 0, 1):
                check(float(base) * sign, (base + offset) * sign, -offset * sign)
    for exponent in (54, 63, 64, 65, 95, 96, 97, 1023):
        base = (2**53 - 1) * 2**(exponent - 53)
        check(float(base), base, 0)
        check(float(base), base - 1, 1)
        check(float(base), base + 1, -1)
    for number in (0.0, -0.0):
        check(number, 0, 0)
        check(number, False, 0)
        check(number, True, -1)
        check(number, -1, 1)
    check(1.0, True, 0)
    for case in ((0.5, 0, 1), (0.5, 1, -1),
                 (-0.5, 0, -1), (-0.5, -1, 1),
                 (1.5, 1, 1), (1.5, 2, -1),
                 (-1.5, -1, -1), (-1.5, -2, 1),
                 (5e-324, 0, 1), (-5e-324, 0, -1),
                 (5e-324, 1, -1), (-5e-324, -1, 1),
                 (2.2250738585072014e-308, 0, 1)):
        number = case[0]
        integer = case[1]
        comparison = case[2]
        check(number, integer, comparison)
    for integer in (0, 1, -1, True, False, 2**2000, -(2**2000)):
        check(float('inf'), integer, 1)
        check(-float('inf'), integer, -1)
        nan = float('nan')
        assert order(nan, integer) == (False, False, False, False)
        assert order(integer, nan) == (False, False, False, False)
        for method in (float.__lt__, float.__le__, float.__gt__, float.__ge__):
            assert method(nan, integer) is False
    check(1.0, 2**2000, -1)
    check(-1.0, -(2**2000), 1)
    for method in (float.__lt__, float.__le__, float.__gt__, float.__ge__):
        for other in (None, '1', [], object()):
            assert method(1.0, other) is NotImplemented
        for receiver in (0, True, None, '1'):
            try:
                method(receiver, 1.0)
            except TypeError:
                pass
            else:
                assert False

test_numeric_boundaries()

class PlainI(int):
    pass


class PlainF(float):
    pass


def test_plain_subclasses():
    for case in ((2**53 + 1, 9007199254740992.0, -1),
                 (2**60, 1152921504606846976.0, 0),
                 (0, -0.0, 0)):
        integer = case[0]
        number = case[1]
        comparison = case[2]
        check(number, PlainI(integer), comparison)
        check(PlainF(number), integer, comparison)
        check(PlainF(number), PlainI(integer), comparison)

test_plain_subclasses()

# Intentional difference from CPython 3.14.5: builtin float methods
# compare subclass payloads without internal comparison/conversion callbacks.
class I(int):
    def __float__(self):
        raise AssertionError('must use payload')
    def __int__(self):
        raise AssertionError('must use payload')
    def __lt__(self, other):
        return 'int lt'
    def __le__(self, other):
        return 'int le'
    def __gt__(self, other):
        return 'int gt'
    def __ge__(self, other):
        return 'int ge'


def test_int_subclass_payloads():
    for base in (0, 1, 2**53, 2**60):
        number = float(base)
        integer = I(base)
        for repeat in range(4):
            assert order(number, integer) == (False, True, False, True)
            assert order(integer, number) == ('int lt', 'int le', 'int gt', 'int ge')
            assert float.__lt__(number, integer) is False
            assert float.__le__(number, integer) is True
            assert float.__gt__(number, integer) is False
            assert float.__ge__(number, integer) is True


    for case in ((9007199254740992.0, I(2**53 + 1), (True, True, False, False)),
                 (-9007199254740992.0, I(-(2**53 + 1)), (False, False, True, True))):
        number = case[0]
        integer = case[1]
        result = case[2]
        assert order(number, integer) == result
        assert (float.__lt__(number, integer), float.__le__(number, integer),
                float.__gt__(number, integer), float.__ge__(number, integer)) == result

test_int_subclass_payloads()

class R:
    def __gt__(self, other):
        return 'reflected gt'
    def __ge__(self, other):
        return 'reflected ge'
    def __lt__(self, other):
        return 'reflected lt'
    def __le__(self, other):
        return 'reflected le'


def test_unrelated_reflection():
    for repeat in range(4):
        assert order(1.0, R()) == ('reflected gt', 'reflected ge', 'reflected lt', 'reflected le')

test_unrelated_reflection()

class F(float):
    pass


def replacement(self, other):
    return 'override'


def test_override_invalidation():
    value = F(2**60)
    number = float(2**60)
    for repeat in range(4):
        assert order(value, 2**60) == (False, True, False, True)
        assert reflected_order(2**60, value) == (False, True, False, True)
        assert subtype_order(number, value) == (False, True, False, True)
    F.__lt__ = replacement
    F.__le__ = replacement
    F.__gt__ = replacement
    F.__ge__ = replacement
    for repeat in range(4):
        assert order(value, 2**60) == ('override', 'override', 'override', 'override')
        assert reflected_order(2**60, value) == ('override', 'override', 'override', 'override')
        assert subtype_order(number, value) == ('override', 'override', 'override', 'override')
        assert float.__lt__(value, 2**60) is False
        assert float.__ge__(value, 2**60) is True
    del F.__lt__
    del F.__le__
    del F.__gt__
    del F.__ge__
    for repeat in range(4):
        assert order(value, 2**60) == (False, True, False, True)
        assert reflected_order(2**60, value) == (False, True, False, True)
        assert subtype_order(number, value) == (False, True, False, True)
    integer = I(1)
    for repeat in range(4):
        assert order(integer, 1.0) == ('int lt', 'int le', 'int gt', 'int ge')
    I.__lt__ = replacement
    for repeat in range(4):
        assert order(integer, 1.0) == ('override', 'int le', 'int gt', 'int ge')
    assert (I(1) < 1.0) == 'override'
    del I.__lt__
    for repeat in range(4):
        assert order(integer, 1.0) == (False, 'int le', 'int gt', 'int ge')
    assert (I(2**60) < float(2**60)) is False

test_override_invalidation()

class DecliningF(float):
    def __gt__(self, other):
        return NotImplemented


def test_container_ordering():
    assert (1.0 < DecliningF(2.0)) is True
    assert order(float('nan'), 1.0) == (False, False, False, False)
    assert order(1.0, float('nan')) == (False, False, False, False)
    items = [9007199254740993, 9007199254740992.0]
    items.sort()
    assert items[0].__class__ is float
    assert items[1].__class__ is int
    # Nested homogeneous containers use the same corrected numeric comparisons.
    for values in ([(2**60 + 1,), (float(2**60),)], [[2**60 + 1], [float(2**60)]]):
        values.sort()
        assert values[0][0].__class__ is float
    first = float(2**60)
    second = 2**60
    values = [first, second]
    values.sort()
    assert values[0] is first
    assert values[1] is second

    values = [9007199254740992.0, 9007199254740993]
    values.sort(reverse=True)
    assert values[0].__class__ is int
    assert values[1].__class__ is float

test_container_ordering()

class DecliningI(int):
    def __lt__(self, other):
        return NotImplemented
    def __le__(self, other):
        return NotImplemented
    def __gt__(self, other):
        return NotImplemented
    def __ge__(self, other):
        return NotImplemented


def test_not_implemented_and_float_pairs():
    for repeat in range(4):
        assert order(DecliningI(2**53 + 1), 9007199254740992.0) == (False, False, True, True)
        assert order(DecliningI(2**60), float(2**60)) == (False, True, False, True)
        assert order(-0.0, 0.0) == (False, True, False, True)
        assert order(float('inf'), float('inf')) == (False, True, False, True)
        assert order(-float('inf'), float('inf')) == (True, True, False, False)
        assert order(float('nan'), float('nan')) == (False, False, False, False)
    for case in ((1.0, -2**2000, 1), (-1.0, 2**2000, -1)):
        number = case[0]
        integer = case[1]
        comparison = case[2]
        check(number, integer, comparison)

test_not_implemented_and_float_pairs()
