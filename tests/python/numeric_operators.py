huge = int("18446744073709551616")

# Power, division, floor division, and modulo preserve builtin numeric semantics.
assert 2 ** 10 == 1024
assert str(2 ** 58) == "288230376151711744"
assert str(2 ** 100) == "1267650600228229401496703205376"
assert str(huge ** 2) == "340282366920938463463374607431768211456"
assert pow(2, 3, None) == 8
assert pow(2, 3, 5) == 3
assert (2).__pow__(3, 5) == 3
assert (2).__rpow__(3, 5) == 4
assert pow(2, -1, 5) == 3
assert pow(2, -1, -5) == -2
assert pow(2, 3, 1) == 0
assert pow(2, 100, 17) == 16
assert str(pow(huge, 2, 97)) == "35"

exponent = -3
assert 2 ** exponent == 0.125
assert (-2) ** exponent == -0.125
exponent = -1
assert huge ** exponent == 5.421010862427522e-20

assert 1 / 2 == 0.5
assert 1.0 / 2 == 0.5
assert 1 / 2.0 == 0.5
assert 1.0 / 2.0 == 0.5

assert 5 // 2 == 2
assert -5 // 2 == -3
assert 5 // -2 == -3
assert -5 // -2 == 2
assert True // 1 == 1
assert str(huge // 3) == "6148914691236517205"
assert str(-huge // 3) == "-6148914691236517206"
assert str(huge // -3) == "-6148914691236517206"
assert str(-huge // -3) == "6148914691236517205"
assert huge // huge == 1
assert 5.0 // 2 == 2.0
assert 5 // 2.0 == 2.0
assert -5.0 // 2 == -3.0

small_negative = -8.122808264515302e-272
large_positive = 7.866340851152702e98
assert small_negative // large_positive == -1.0
assert large_positive.__rfloordiv__(small_negative) == -1.0

assert 5 % 2 == 1
assert -5 % 2 == 1
assert 5 % -2 == -1
assert -5 % -2 == -1
assert False % 1 == 0
assert huge % 3 == 1
assert -huge % 3 == 2
assert huge % -3 == -2
assert -huge % -3 == -1
assert huge % huge == 0
assert 5.0 % 2 == 1.0
assert 5 % 2.0 == 1.0
assert -5.0 % 2 == 1.0
assert 5.0 % -2 == -1.0
assert small_negative % large_positive == large_positive
assert large_positive.__rmod__(small_negative) == large_positive


# Binary arithmetic dispatch calls Python methods and continues to reflected methods after NotImplemented.
class CustomAdd:
    def __add__(self, other):
        return 11


assert CustomAdd() + 1 == 11


class NotImplementedAdd:
    def __add__(self, other):
        return NotImplemented


class ReflectedAdd:
    def __radd__(self, other):
        return 7


assert NotImplementedAdd() + ReflectedAdd() == 7


class NotImplementedMatmul:
    def __matmul__(self, other):
        return NotImplemented


class ReflectedMatmul:
    def __rmatmul__(self, other):
        return 7


assert NotImplementedMatmul() @ ReflectedMatmul() == 7


# Bitwise operators dispatch to reflected and unary custom methods.
class ReflectedBitwise:
    def __rand__(self, other):
        return 42

    def __ror__(self, other):
        return 43

    def __rxor__(self, other):
        return 44


class CustomInvert:
    def __invert__(self):
        return 45


assert 1 & ReflectedBitwise() == 42
assert 1 | ReflectedBitwise() == 43
assert 1 ^ ReflectedBitwise() == 44
assert ~CustomInvert() == 45


# Binary and builtin power dispatch distinguish absent, None, and explicit modulo arguments.
class BinaryPower:
    def __pow__(self, other):
        return 42


class ReflectedBinaryPower:
    def __rpow__(self, other):
        return 43


class OptionalModuloPower:
    def __pow__(self, other, modulo=None):
        if modulo is None:
            return 44
        return 45


assert BinaryPower() ** 3 == 42
assert 2 ** ReflectedBinaryPower() == 43
assert OptionalModuloPower() ** 3 == 44
assert pow(BinaryPower(), 3) == 42


class BinaryPowerThroughPow:
    def __pow__(self, other):
        return 43


assert pow(BinaryPowerThroughPow(), 3, None) == 43


class TernaryPower:
    def __pow__(self, other, modulo):
        return other * 100 + modulo


class ReflectedTernaryPower:
    def __rpow__(self, other, modulo):
        return 45


assert pow(TernaryPower(), 3, 5) == 305
assert pow(2, ReflectedTernaryPower(), 5) == 45


# An inherited reflected method does not get subclass priority over __add__.
class InheritedReflectedAddBase:
    def __add__(self, other):
        return 3

    def __radd__(self, other):
        return 7


class InheritedReflectedAddDerived(InheritedReflectedAddBase):
    pass


assert InheritedReflectedAddBase() + InheritedReflectedAddDerived() == 3
