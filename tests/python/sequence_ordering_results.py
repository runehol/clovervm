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


class IdentityOnly:
    def __eq__(self, other):
        raise AssertionError('identity must skip equality')

    def __lt__(self, other):
        raise AssertionError('identity must skip ordering')


same = IdentityOnly()
for sequence in (list, tuple):
    assert make_sequence(sequence, (same,)) < make_sequence(sequence, (same, 1))
    assert make_sequence(sequence, (same,)) <= make_sequence(sequence, (same,))
    assert make_sequence(sequence, (same,)) >= make_sequence(sequence, (same,))


# The element comparison's result is returned unchanged, including falsey values.
result = []
events = []


class Truth:
    def __bool__(self):
        events.append('bool')
        return False


class Element:
    def __eq__(self, other):
        events.append('eq')
        return Truth()

    def __lt__(self, other):
        events.append('lt')
        return result

    def __le__(self, other):
        events.append('le')
        return result

    def __gt__(self, other):
        events.append('gt')
        return result

    def __ge__(self, other):
        events.append('ge')
        return result


for sequence in (list, tuple):
    for case in ((lt, 'lt'), (le, 'le'), (gt, 'gt'), (ge, 'ge')):
        compare = case[0]
        name = case[1]
        for i in range(3):
            events.clear()
            assert compare(make_sequence(sequence, (Element(),)), make_sequence(sequence, (Element(),))) is result
            assert events == ['eq', 'bool', name]


class Reflected:
    def __lt__(self, other):
        return 'reflected lt'

    def __le__(self, other):
        return 'reflected le'

    def __gt__(self, other):
        return 'reflected gt'

    def __ge__(self, other):
        return 'reflected ge'


for sequence in (list, tuple):
    for i in range(3):
        assert lt(make_sequence(sequence), Reflected()) == 'reflected gt'
        assert le(make_sequence(sequence), Reflected()) == 'reflected ge'
        assert gt(make_sequence(sequence), Reflected()) == 'reflected lt'
        assert ge(make_sequence(sequence), Reflected()) == 'reflected le'
        assert lt(make_sequence(sequence, (object(),)), make_sequence(sequence, (Reflected(),))) == 'reflected gt'
