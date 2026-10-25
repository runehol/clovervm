# CPython 3.14.5 rich-comparison rules, exercised at warmed operator sites.
def lt(a, b):
    return a < b


def le(a, b):
    return a <= b


def gt(a, b):
    return a > b


def ge(a, b):
    return a >= b


class Truth:
    def __bool__(self):
        return True


class Field:
    def __eq__(self, other):
        return Truth()

    def __lt__(self, other):
        return 'lt'

    def __le__(self, other):
        return 'le'

    def __gt__(self, other):
        return 'gt'

    def __ge__(self, other):
        return 'ge'


a = slice(Field())
b = slice(Field())
for i in range(100):
    assert lt(a, b) is False
    assert le(a, b) is True
    assert gt(a, b) is False
    assert ge(a, b) is True


def false_bool(self):
    return False


Truth.__bool__ = false_bool
for i in range(100):
    assert lt(a, b) == 'lt'
    assert le(a, b) == 'le'
    assert gt(a, b) == 'gt'
    assert ge(a, b) == 'ge'
del Truth.__bool__
for i in range(100):
    assert lt(a, b) is False
    assert le(a, b) is True


def false_eq(self, other):
    return False


Field.__eq__ = false_eq
for i in range(100):
    assert lt(a, b) == 'lt'
    assert ge(a, b) == 'ge'


def replacement(self, other):
    return 'replaced'


Field.__lt__ = replacement
Field.__le__ = replacement
Field.__gt__ = replacement
Field.__ge__ = replacement
for op in (lt, le, gt, ge):
    for i in range(100):
        assert op(a, b) == 'replaced'
# Deleted equality falls back to identity, then ordering still runs.
del Field.__eq__
for i in range(100):
    assert lt(a, b) == 'replaced'
# Deleting ordering methods must invalidate previously warmed dispatch.
del Field.__lt__
del Field.__le__
del Field.__gt__
del Field.__ge__
for op in (lt, le, gt, ge):
    try:
        op(a, b)
    except TypeError:
        pass
    else:
        assert False, 'deleted ordering must not be replayed'

# An earlier callback can change the later field's comparison within one call.
class Later:
    def __eq__(self, other):
        return True

    def __lt__(self, other):
        return 'later'


class Earlier:
    def __eq__(self, other):
        Later.__eq__ = false_eq
        return True


assert lt(slice(Earlier(), Later()), slice(Earlier(), Later())) == 'later'

# Reentry and owner removal during both equality and ordering callbacks.
owners = []
changed_left = []
changed_right = [7]


class Reenter:
    def __eq__(self, other):
        owners.clear()
        changed_left.append(7)
        assert lt(slice(slice(1)), slice(slice(2)))
        junk = []
        for i in range(50):
            junk.append(list(range(20)))
        assert len(junk) == 50
        return True


owners.append(slice(Reenter(), changed_left))
owners.append(slice(Reenter(), changed_right))
assert le(owners[0], owners[1])
assert owners == []


class OrderReenter:
    def __init__(self, marker):
        self.marker = marker

    def __eq__(self, other):
        return False

    def __lt__(self, other):
        owners.clear()
        junk = []
        for i in range(50):
            junk.append(list(range(20)))
        assert self.marker == 7
        assert other.marker == 8
        assert lt(slice((1,)), slice((2,)))
        return self


owners.append(slice(OrderReenter(7)))
owners.append(slice(OrderReenter(8)))
assert lt(owners[0], owners[1]).marker == 7
assert owners == []
