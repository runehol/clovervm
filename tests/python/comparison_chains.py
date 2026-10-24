# Intermediate comparison operands must never overwrite the initial local.
def local_operand():
    value = 1
    result = value < 2 < 3 < 4 < 5 < 6
    assert result
    assert value == 1
local_operand()


def parameter_operand(value):
    result = value < 2 < 3 < 4
    assert result
    assert value == 1
parameter_operand(1)


# Later operands must observe the original value, including when that changes
# whether the chain succeeds.
def later_read(value):
    assert not (value < 2 < 3 < value + 2)
    assert value == 1
later_read(1)


# Computed initial operands and shorter chains use the same evaluation rules.
def computed_operand(value):
    assert value + 1 < 3 < 4 < 5 < 6
    assert value == 1
    assert value < 2
    assert value < 2 < 3
    assert value == 1
computed_operand(1)


# Each evaluated operand runs once, and nothing past a false comparison runs.
events = []
def operand(value):
    events.append(value)
    return value
assert operand(1) < operand(2) < operand(3) < operand(4) < operand(5)
assert events == [1, 2, 3, 4, 5]
events = []
assert not (operand(3) < operand(2) < operand(4) < operand(5))
assert events == [3, 2]
events = []
assert not (operand(1) < operand(3) < operand(2) < operand(4) < operand(5))
assert events == [1, 3, 2]


# A successful chain returns the final comparison's object, without coercing it.
class Result:
    def __init__(self, value):
        self.value = value
    def __bool__(self):
        events.append(self.value)
        return True


class Operand:
    def __init__(self, result):
        self.result = result
    def __lt__(self, other):
        return self.result


first = Result(1)
second = Result(2)
last = Result(3)
events = []
assert (Operand(first) < Operand(second) < Operand(last) < Operand(None)) is last
assert events == [1, 2]
