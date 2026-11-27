# Test both sides of CloverVM's signed 59-bit immediate integer range.
# Function arguments reach the register opcodes. Literal right operands also
# exercise the immediate opcodes, including augmented assignment.
MAX_SMI = (1 << 58) - 1
MIN_SMI = -(1 << 58)


def add(left, right, expected):
    assert left + right == expected
    left += right
    assert left == expected


def subtract(left, right, expected):
    assert left - right == expected
    left -= right
    assert left == expected


def multiply(left, right, expected):
    assert left * right == expected
    left *= right
    assert left == expected


def floor_divide(left, right, expected):
    assert left // right == expected
    left //= right
    assert left == expected


def left_shift(left, right, expected):
    assert left << right == expected
    left <<= right
    assert left == expected


def add_immediate_above(left, expected):
    assert left + 1 == expected
    left += 1
    assert left == expected


def subtract_immediate_below(left, expected):
    assert left - 1 == expected
    left -= 1
    assert left == expected


def multiply_immediate(left, expected):
    assert left * 2 == expected
    left *= 2
    assert left == expected


def left_shift_immediate(left, expected):
    assert left << 1 == expected
    left <<= 1
    assert left == expected


for _ in range(3):
    add(MAX_SMI, 1, 1 << 58)
    add(MIN_SMI, -1, -(1 << 58) - 1)
    add_immediate_above(MAX_SMI, 1 << 58)
    subtract(MAX_SMI, -2, (1 << 58) + 1)
    subtract(MIN_SMI, 1, -(1 << 58) - 1)
    subtract_immediate_below(MIN_SMI, -(1 << 58) - 1)
    multiply(MAX_SMI, 2, (1 << 59) - 2)
    multiply(MIN_SMI, 2, -(1 << 59))
    multiply_immediate(MAX_SMI, (1 << 59) - 2)
    multiply_immediate(MIN_SMI, -(1 << 59))
    floor_divide(MIN_SMI, -1, 1 << 58)
    left_shift(MAX_SMI, 1, (1 << 59) - 2)
    left_shift(MIN_SMI, 1, -(1 << 59))
    left_shift(1, 64, 1 << 64)
    left_shift(-1, 64, -(1 << 64))
    left_shift_immediate(MAX_SMI, (1 << 59) - 2)
    left_shift_immediate(MIN_SMI, -(1 << 59))
    assert -MIN_SMI == 1 << 58
