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


# Inherited builtin methods compare payloads without invoking sequence overrides.
class ListSubclass(list):
    def __len__(self):
        raise AssertionError('native payload length expected')

    def __getitem__(self, index):
        raise AssertionError('native payload item expected')


for i in range(3):
    assert lt(ListSubclass((1,)), [2])
    assert gt([2], ListSubclass((1,)))


class OverrideList(list):
    def __lt__(self, other):
        return 'sub lt'

    def __le__(self, other):
        return 'sub le'

    def __gt__(self, other):
        return 'sub gt'

    def __ge__(self, other):
        return 'sub ge'


for i in range(3):
    assert lt([], OverrideList()) == 'sub gt'
    assert le([], OverrideList()) == 'sub ge'
    assert gt([], OverrideList()) == 'sub lt'
    assert ge([], OverrideList()) == 'sub le'
    assert lt(OverrideList(), []) == 'sub lt'
    assert le(OverrideList(), []) == 'sub le'
    assert gt(OverrideList(), []) == 'sub gt'
    assert ge(OverrideList(), []) == 'sub ge'


def changed(self, other):
    return 'changed'


OverrideList.__gt__ = changed
assert lt([], OverrideList()) == 'changed'
del OverrideList.__gt__
assert lt([], OverrideList((1,))) is True


# Explicit builtin calls bypass subclass special-method overrides.
assert list.__lt__(OverrideList((1,)), [2]) is True
assert list.__gt__([2], OverrideList((1,))) is True
