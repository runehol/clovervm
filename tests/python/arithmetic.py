assert 1 + 2 * (4 + 3) == 15
assert (1 << 4) + 3 == 19
assert (not True) == False
assert 1 - 2 * (4 + 3) == -13

assert -(3 + 4) == -7
assert (-1) << 58 == -288230376151711743 - 1
assert (-9) >> 1 == -5
assert -288230376151711743 - 1 == (-1) << 58

# int construction handles defaults, exact integers, booleans, and string parsing.
assert int() == 0
assert int(42) == 42

large_int = int("288230376151711744")
assert str(int(large_int)) == "288230376151711744"
assert int(True) == 1
assert int(False) == 0
assert int("123") == 123
assert int("  -42  ") == -42
assert int("1_000") == 1000
assert str(int("288_230_376_151_711_744")) == "288230376151711744"

# Arbitrary-precision integers compare correctly with integers and booleans.
assert large_int == int("288230376151711744")
assert large_int > 288230376151711743
assert int("-288230376151711745") < int("-288230376151711744")
assert large_int > True


# Rich comparison methods return their result objects without coercing to bool.
class ComparisonResults:
    def __eq__(self, other):
        return "eq"

    def __ne__(self, other):
        return "ne"

    def __lt__(self, other):
        return "lt"

    def __le__(self, other):
        return "le"

    def __gt__(self, other):
        return "gt"

    def __ge__(self, other):
        return "ge"


assert (ComparisonResults() == ComparisonResults()) == "eq"
assert (ComparisonResults() != ComparisonResults()) == "ne"
assert (ComparisonResults() < ComparisonResults()) == "lt"
assert (ComparisonResults() <= ComparisonResults()) == "le"
assert (ComparisonResults() > ComparisonResults()) == "gt"
assert (ComparisonResults() >= ComparisonResults()) == "ge"


# Matrix multiplication dispatches to Python methods for binary and augmented forms.
class Matrix:
    def __matmul__(self, other):
        return "matmul"


assert Matrix() @ Matrix() == "matmul"
matrix = Matrix()
matrix @= Matrix()
assert matrix == "matmul"

# Large shift counts saturate right shifts and promote overflowing left shifts.
assert 1 >> 63 == 0
assert 1 >> 64 == 0
assert 1 >> 127 == 0
assert -9 >> 63 == -1
assert -9 >> 64 == -1
assert -9 >> 127 == -1

positive = 1
negative = -9
shift = 63
assert positive >> shift == 0
assert negative >> shift == -1
shift = 64
assert positive >> shift == 0
assert negative >> shift == -1
shift = 128
assert positive >> shift == 0
assert negative >> shift == -1

assert str(1 << 58) == "288230376151711744"
shift = 58
assert str(1 << shift) == "288230376151711744"

huge = int("18446744073709551616")
assert str(huge << 4) == "295147905179352825856"
assert str(-huge << 1) == "-36893488147419103232"
assert huge >> 64 == 1
assert -huge >> 64 == -1
assert -(huge + 1) >> 64 == -2
assert huge >> huge == 0
assert -huge >> huge == -1

assert str(~huge) == "-18446744073709551617"
assert str(~-huge) == "18446744073709551615"
assert str(huge | 3) == "18446744073709551619"
assert -huge & 255 == 0
assert str(-huge | 255) == "-18446744073709551361"
assert str(-huge ^ 255) == "-18446744073709551361"
assert str(-huge & -255) == "-18446744073709551616"

# Integer arithmetic and direct dunder calls promote overflowing results.
assert str(288230376151711743 + 1) == "288230376151711744"
assert str(-288230376151711743 - 2) == "-288230376151711745"
assert str(288230376151711743 * 2) == "576460752303423486"
assert str(+large_int) == "288230376151711744"
assert +True == 1

assert str((288230376151711743).__add__(1)) == "288230376151711744"
assert str((-288230376151711743).__sub__(2)) == "-288230376151711745"
assert str((288230376151711743).__mul__(2)) == "576460752303423486"

assert str(large_int + 1) == "288230376151711745"
assert large_int - large_int == 0
assert str(large_int * -2) == "-576460752303423488"
assert str(True + large_int) == "288230376151711745"

# Float and integer arithmetic preserve numeric values across mixed bool and int operands.
assert 1.5 + 2.25 == 3.75
assert 1.5 + 2 == 3.5
assert 2 + 1.5 == 3.5
assert 5.5 - 2.0 == 3.5
assert 5.5 - 2 == 3.5
assert 5 - 1.5 == 3.5
assert 1.5 * 2.0 == 3.0
assert 1.5 * 2 == 3.0
assert 2 * 1.5 == 3.0
assert 1.0 + True == 2.0
assert True + 1.0 == 2.0
assert -1.5 == -1.5
assert +1.5 == 1.5
assert not -0.0
assert 1 + 2 == 3
assert 5 - 2 == 3
assert 2 * 3 == 6
assert 1 + True == 2
assert True + 1 == 2
assert 1 - True == 0
assert True - 1 == 0
assert 1 * True == 1
assert True * 1 == 1
assert True + True == 2
assert True - True == 0
assert True * False == 0
assert -True == -1
assert +True == 1

# Bitwise operations handle positive and negative integers.
assert 5 & 3 == 1
assert 4 | 1 == 5
assert 6 ^ 3 == 5
assert -8 | 3 == -5
assert -8 & 3 == 0
assert -8 ^ 3 == -5
assert ~3 == -4
assert ~-3 == 2
