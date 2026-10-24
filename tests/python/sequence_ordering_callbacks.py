# Shared CPython 3.14.5 / CloverVM semantic regression coverage.
def lt(a, b):
    return a < b


def le(a, b):
    return a <= b


def gt(a, b):
    return a > b


def ge(a, b):
    return a >= b


# Exact tuple construction uses literals; tuple() allocation is unavailable too.
def make_sequence(kind, items=()):
    if kind is list:
        return list(items)
    return items


operations = (lt, le, gt, ge)


def changed(self, other):
    return 'changed'


# Tuple subclass construction remains unavailable in CloverVM. Reachable tuple
# coverage above exercises nested payloads and reflected element dispatch.

# The shared element-ordering bytecode helper must invalidate its cache too.
class MutableElement:
    def __eq__(self, other):
        return False

    def __lt__(self, other):
        return 'before'


for i in range(3):
    assert lt([MutableElement()], [MutableElement()]) == 'before'
MutableElement.__lt__ = changed
for i in range(3):
    assert lt([MutableElement()], [MutableElement()]) == 'changed'
    assert lt((MutableElement(),), (MutableElement(),)) == 'changed'

# Ordering itself may remove the only container references to its arguments.
class ClearOnOrder:
    def __init__(self, marker):
        self.marker = marker

    def __eq__(self, other):
        return False

    def __lt__(self, other):
        left.clear()
        right.clear()
        garbage = []
        for i in range(100):
            garbage.append(ClearOnOrder(i))
        assert self.marker == 7
        assert other.marker == 8
        assert (1,) < (2,)
        return self


left = [ClearOnOrder(7)]
right = [ClearOnOrder(8)]
assert lt(left, right).marker == 7


# Truth conversion of equality can resize the lists before the order step.
class ClearTruth:
    def __bool__(self):
        left.clear()
        return False


class ClearThroughTruth:
    def __eq__(self, other):
        return ClearTruth()


left = [ClearThroughTruth()]
right = [object()]
assert lt(left, right) is True


nan = float('nan')
for sequence in (list, tuple):
    assert make_sequence(sequence, (nan,)) <= make_sequence(sequence, (nan,))
    assert make_sequence(sequence, (nan,)) >= make_sequence(sequence, (nan,))
