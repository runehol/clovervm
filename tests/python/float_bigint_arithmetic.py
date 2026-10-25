import math


# Distinct sites retain each operand order through warm replay.
def add_forward(left, right):
    return left + right


def add_reverse(left, right):
    return left + right


def sub_forward(left, right):
    return left - right


def sub_reverse(left, right):
    return left - right


def mul_forward(left, right):
    return left * right


def mul_reverse(left, right):
    return left * right


def truediv_forward(left, right):
    return left / right


def truediv_reverse(left, right):
    return left / right


def floordiv_forward(left, right):
    return left // right


def floordiv_reverse(left, right):
    return left // right


def mod_forward(left, right):
    return left % right


def mod_reverse(left, right):
    return left % right


operations = (
    (add_forward, add_reverse, float.__add__, float.__radd__,
     int.__add__, int.__radd__),
    (sub_forward, sub_reverse, float.__sub__, float.__rsub__,
     int.__sub__, int.__rsub__),
    (mul_forward, mul_reverse, float.__mul__, float.__rmul__,
     int.__mul__, int.__rmul__),
    (truediv_forward, truediv_reverse, float.__truediv__, float.__rtruediv__,
     int.__truediv__, int.__rtruediv__),
    (floordiv_forward, floordiv_reverse, float.__floordiv__, float.__rfloordiv__,
     int.__floordiv__, int.__rfloordiv__),
    (mod_forward, mod_reverse, float.__mod__, float.__rmod__,
     int.__mod__, int.__rmod__),
)

# Expected values are CPython binary64 results, including conversion ties.
cases = ((1152921504606846976,
  [(1.152921504606847e+18, 1.152921504606847e+18),
   (-1.152921504606847e+18, 1.152921504606847e+18),
   (4.0352252661239644e+18, 4.0352252661239644e+18),
   (3.0357660829594124e-18, 3.294061441733848e+17),
   (0.0, 3.294061441733848e+17),
   (3.5, 1.0)]),
 (1152921504606847104,
  [(1.152921504606847e+18, 1.152921504606847e+18),
   (-1.152921504606847e+18, 1.152921504606847e+18),
   (4.0352252661239644e+18, 4.0352252661239644e+18),
   (3.0357660829594124e-18, 3.294061441733848e+17),
   (0.0, 3.294061441733848e+17),
   (3.5, 1.0)]),
 (1152921504606847105,
  [(1.1529215046068472e+18, 1.1529215046068472e+18),
   (-1.1529215046068472e+18, 1.1529215046068472e+18),
   (4.0352252661239654e+18, 4.0352252661239654e+18),
   (3.0357660829594116e-18, 3.294061441733849e+17),
   (0.0, 3.294061441733849e+17),
   (3.5, 1.5)]),
 (1152921504606847360,
  [(1.1529215046068475e+18, 1.1529215046068475e+18),
   (-1.1529215046068475e+18, 1.1529215046068475e+18),
   (4.0352252661239665e+18, 4.0352252661239665e+18),
   (3.0357660829594113e-18, 3.29406144173385e+17),
   (0.0, 3.29406144173385e+17),
   (3.5, 2.0)]),
 (-1152921504606846976,
  [(-1.152921504606847e+18, -1.152921504606847e+18),
   (1.152921504606847e+18, -1.152921504606847e+18),
   (-4.0352252661239644e+18, -4.0352252661239644e+18),
   (-3.0357660829594124e-18, -3.294061441733848e+17),
   (-1.0, -3.294061441733848e+17),
   (-1.152921504606847e+18, 2.5)]),
 (-1152921504606847105,
  [(-1.1529215046068472e+18, -1.1529215046068472e+18),
   (1.1529215046068472e+18, -1.1529215046068472e+18),
   (-4.0352252661239654e+18, -4.0352252661239654e+18),
   (-3.0357660829594116e-18, -3.294061441733849e+17),
   (-1.0, -3.294061441733849e+17),
   (-1.1529215046068472e+18, 2.0)]))
def test_finite_results():
    for case in cases:
        integer = case[0]
        expected = case[1]
        for index in range(6):
            forward = operations[index][0]
            reverse = operations[index][1]
            direct = operations[index][2]
            reflected = operations[index][3]
            int_direct = operations[index][4]
            int_reflected = operations[index][5]
            for repeat in range(4):
                result = forward(3.5, integer)
                assert result.__class__ is float and result == expected[index][0]
                result = reverse(integer, 3.5)
                assert result.__class__ is float and result == expected[index][1]
                assert direct(3.5, integer) == expected[index][0]
                assert reflected(3.5, integer) == expected[index][1]
                assert int_direct(integer, 3.5) is NotImplemented
                assert int_reflected(integer, 3.5) is NotImplemented


test_finite_results()


def overflow(function, left, right):
    try:
        function(left, right)
    except OverflowError as error:
        assert str(error) == "int too large to convert to float"
    else:
        assert False


huge = 10**400
# Warm with a representable heap int, then overflow at the same shape/site.
def test_overflow_precedence():
    for operation in operations:
        forward = operation[0]
        reverse = operation[1]
        direct = operation[2]
        reflected = operation[3]
        int_direct = operation[4]
        int_reflected = operation[5]
        for integer in (huge, -huge):
            for number in (0.0, -0.0, 1.0, -1.0, math.inf, math.nan):
                for repeat in range(4):
                    forward(number, 2**60)
                    reverse(2**60, 1.0)
                    overflow(forward, number, integer)
                    overflow(reverse, integer, number)
                    overflow(direct, number, integer)
                    overflow(reflected, number, integer)


test_overflow_precedence()

# Largest rounding bin: just below the overflow tie succeeds; tie overflows.
threshold = 2**1024 - 2**970
def test_overflow_boundary():
    for sign in (1, -1):
        maximum = sign * (threshold - 1)
        expected = sign * 1.7976931348623157e308
        assert add_forward(0.0, maximum) == expected
        assert add_reverse(maximum, 0.0) == expected
        assert mul_forward(1.0, maximum) == expected
        assert mul_reverse(maximum, 1.0) == expected
        overflow(add_forward, 0.0, sign * threshold)
        overflow(add_reverse, sign * threshold, 0.0)
        # Arithmetic overflow after a successful conversion produces infinity.
        assert mul_forward(2.0, maximum) == sign * math.inf
        assert mul_reverse(maximum, 2.0) == sign * math.inf


test_overflow_boundary()


def zero_error(function, left, right):
    try:
        function(left, right)
    except ZeroDivisionError:
        pass
    else:
        assert False


def test_zero_divisors():
    for index in (3, 4, 5):
        forward = operations[index][0]
        reverse = operations[index][1]
        direct = operations[index][2]
        reflected = operations[index][3]
        int_direct = operations[index][4]
        int_reflected = operations[index][5]
        for sign in (1, -1):
            integer = sign * 2**60
            for zero in (0.0, -0.0):
                for repeat in range(4):
                    reverse(integer, 1.0)
                    zero_error(reverse, integer, zero)
                    zero_error(reflected, zero, integer)
                result = forward(zero, integer)
                assert result == 0.0
                expected_sign = sign if index == 5 else math.copysign(1.0, zero) * sign
                assert math.copysign(1.0, result) == expected_sign


test_zero_divisors()


class PlainI(int):
    def __float__(self):
        assert False

    def __index__(self):
        assert False


class PlainF(float):
    pass


class Hooks:
    def __float__(self):
        assert False

    def __index__(self):
        assert False


def test_subclass_payloads():
    for index in range(6):
        forward = operations[index][0]
        reverse = operations[index][1]
        direct = operations[index][2]
        reflected = operations[index][3]
        int_direct = operations[index][4]
        int_reflected = operations[index][5]
        for repeat in range(4):
            assert forward(PlainF(3.5), PlainI(2**60)) == cases[0][1][index][0]
            assert reverse(PlainI(2**60), PlainF(3.5)) == cases[0][1][index][1]
            assert direct(PlainF(3.5), PlainI(2**60)) == cases[0][1][index][0]
            assert reflected(PlainF(3.5), PlainI(2**60)) == cases[0][1][index][1]
        for method in (direct, reflected):
            assert method(1.0, Hooks()) is NotImplemented
            assert method(1.0, None) is NotImplemented
            # Receiver errors precede conversion overflow.
            try:
                method(1, huge)
            except TypeError:
                pass
            else:
                assert False


test_subclass_payloads()


class OverrideI(int):
    def __add__(self, other):
        return 71

    def __radd__(self, other):
        return 72


# float accepts an int payload before the unrelated int subtype's reflection.
def test_overrides():
    for repeat in range(4):
        assert add_forward(1.0, OverrideI(2**60)) == 1152921504606846976.0
        assert add_reverse(OverrideI(2**60), 1.0) == 71
        assert float.__add__(1.0, OverrideI(2**60)) == 1152921504606846976.0


test_overrides()


class Fallback:
    def __radd__(self, other):
        return 81


def test_addition_fallback():
    for repeat in range(4):
        assert add_forward(1.0, Fallback()) == 81


test_addition_fallback()


# Keep existing SMI/bool/float paths and exact comparison behavior covered.
def test_existing_fast_paths():
    for repeat in range(4):
        assert add_forward(1.0, True) == 2.0
        assert sub_forward(3.5, 2) == 1.5
        assert mul_forward(1.5, 2.0) == 3.0
        assert truediv_forward(3.0, 2) == 1.5
        assert floordiv_forward(-3.0, 2) == -2.0
        assert mod_forward(-3.0, 2) == 1.0
        assert 1152921504606846976.0 != 2**60 + 1
        assert 1152921504606846976.0 < 2**60 + 1
        assert 1.0 < huge


test_existing_fast_paths()


class ChangingI(int):
    def __add__(self, other):
        return NotImplemented


def changing_add(left, right):
    return left + right


value = ChangingI(2**60)
def test_before_mutation():
    for repeat in range(4):
        assert changing_add(value, 1.0) == 1152921504606846976.0


test_before_mutation()


def changed_add(self, other):
    return 91


ChangingI.__add__ = changed_add
def test_after_mutation():
    for repeat in range(4):
        assert changing_add(value, 1.0) == 91


test_after_mutation()


# Int subclasses use heap layout even when their value is zero or small.
def test_subclass_zero():
    for index in (3, 4, 5):
        forward = operations[index][0]
        reverse = operations[index][1]
        direct = operations[index][2]
        reflected = operations[index][3]
        int_direct = operations[index][4]
        int_reflected = operations[index][5]
        zero_error(forward, 1.0, PlainI(0))
        zero_error(direct, 1.0, PlainI(0))


test_subclass_zero()


class ReflectedFallback:
    def __rsub__(self, other):
        return 82

    def __rmul__(self, other):
        return 83

    def __rtruediv__(self, other):
        return 84

    def __rfloordiv__(self, other):
        return 85

    def __rmod__(self, other):
        return 86


fallback = ReflectedFallback()
def test_other_fallbacks():
    for repeat in range(4):
        assert sub_forward(1.0, fallback) == 82
        assert mul_forward(1.0, fallback) == 83
        assert truediv_forward(1.0, fallback) == 84
        assert floordiv_forward(1.0, fallback) == 85
        assert mod_forward(1.0, fallback) == 86


test_other_fallbacks()
