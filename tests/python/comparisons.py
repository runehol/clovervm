# Float comparisons cover float, int, bool, signed zero, infinity, and NaN values.
assert 1.0 < 2.0
assert not 2.0 < 1.0
assert 1.0 <= 1.0
assert not 2.0 <= 1.0
assert 2.0 > 1.0
assert not 1.0 > 2.0
assert 2.0 >= 2.0
assert not 1.0 >= 2.0
assert 1.0 == 1.0
assert not 1.0 == 2.0
assert 1.0 != 2.0
assert not 1.0 != 1.0

assert 1 < 2.0
assert not 2 < 1.0
assert 1 <= 1.0
assert not 2 <= 1.0
assert 2 > 1.0
assert not 1 > 2.0
assert 2 >= 2.0
assert not 1 >= 2.0
assert 1 == 1.0
assert not 1 == 2.0
assert 1 != 2.0
assert not 1 != 1.0

assert 1.0 < 2
assert not 2.0 < 1
assert 1.0 <= 1
assert not 2.0 <= 1
assert 2.0 > 1
assert not 1.0 > 2
assert 2.0 >= 2
assert not 1.0 >= 2
assert 1.0 == 1
assert not 1.0 == 2
assert 1.0 != 2
assert not 1.0 != 1
assert True == 1.0
assert False == 0.0
assert not True != 1.0
assert not False != 0.0
assert True < 2.0
assert False >= 0.0

assert 0.0 == -0.0
assert not 0.0 != -0.0
assert 0.0 <= -0.0
assert 0.0 >= -0.0

infinity = 1e300 * 1e300
nan = infinity / infinity
assert not nan < nan
assert not nan <= nan
assert not nan > nan
assert not nan >= nan
assert not nan == nan
assert nan != nan


# Equality dispatch handles identity fallback, != inversion, double dispatch, and subclass priority.
class NotImplementedEquality:
    def __eq__(self, other):
        return NotImplemented


same_identity = NotImplementedEquality()
different_identity = NotImplementedEquality()
assert same_identity == same_identity
assert not same_identity == different_identity


class AlwaysEqual:
    def __eq__(self, other):
        return True


class NeverEqual:
    def __eq__(self, other):
        return False


assert not AlwaysEqual() != AlwaysEqual()
assert NeverEqual() != NeverEqual()


class CountEqualityCalls:
    count = 0

    def __eq__(self, other):
        CountEqualityCalls.count += 1
        return NotImplemented


CountEqualityCalls() == CountEqualityCalls()
assert CountEqualityCalls.count == 2


class EqualityBase:
    def __eq__(self, other):
        return 3


class EqualityDerived(EqualityBase):
    def __eq__(self, other):
        return 7


assert (EqualityBase() == EqualityDerived()) == 7


# Repeated comparisons exercise trusted cache hits and reload methods after NotImplemented.
def repeated_eq(left, right):
    return left == right


assert repeated_eq(1, 1.0)
assert repeated_eq(1, 1.0)


def repeated_lt(left, right):
    return left < right


assert not repeated_lt(2, 1.0)
assert not repeated_lt(2, 1.0)


class ReplacingEquality:
    count = 0

    def __eq__(self, other):
        ReplacingEquality.count += 1

        def replacement(self, other):
            ReplacingEquality.count += 100
            return 42

        ReplacingEquality.__eq__ = replacement
        return NotImplemented


ReplacingEquality() == ReplacingEquality()
assert ReplacingEquality.count == 101
