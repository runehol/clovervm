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


class EqRaises:
    def __eq__(self, other):
        raise ValueError('equality')


class BoolRaises:
    def __bool__(self):
        raise ValueError('truth')


class EqBoolRaises:
    def __eq__(self, other):
        return BoolRaises()


class OrderRaises:
    def __eq__(self, other):
        return False

    def __lt__(self, other):
        raise ValueError('ordering')

    __le__ = __lt__
    __gt__ = __lt__
    __ge__ = __lt__


for sequence in (list, tuple):
    for compare in operations:
        for case in ((EqRaises, 'equality'), (EqBoolRaises, 'truth'), (OrderRaises, 'ordering')):
            element = case[0]
            message = case[1]
            try:
                compare(make_sequence(sequence, (element(),)), make_sequence(sequence, (object(),)))
                assert False
            except ValueError as error:
                assert str(error) == message


# Equality may clear both lists, replace the unequal index, or extend a prefix.
left = []
right = []


class Clear:
    def __eq__(self, other):
        left.clear()
        right.clear()
        # Exercise reentry and allocation after removing both compared elements.
        assert [1] < [2]
        garbage = []
        for i in range(100):
            garbage.append(Clear())
        return False

    def __lt__(self, other):
        raise AssertionError('cleared index must not be ordered')


for case in ((lt, False), (le, True), (gt, False), (ge, True)):
    compare = case[0]
    expected = case[1]
    left = [Clear()]
    right = [Clear()]
    assert compare(left, right) is expected


class Replace:
    def __eq__(self, other):
        left[0] = 5
        right[0] = 2
        return False

    def __lt__(self, other):
        raise AssertionError('reload replaced element')


left = [Replace()]
right = [Replace()]
assert lt(left, right) is False


class Grow:
    def __eq__(self, other):
        left.extend([3, 4, 5])
        right.extend([2, 4, 5])
        return True


left = [Grow()]
right = [Grow()]
assert gt(left, right) is True
