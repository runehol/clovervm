# Shared CPython 3.14.5 / CloverVM semantic regression coverage.
def lt(a, b):
    return a < b


def le(a, b):
    return a <= b


def gt(a, b):
    return a > b


def ge(a, b):
    return a >= b


def make_sequence(kind, items=()):
    return kind(items)


operations = (lt, le, gt, ge)


for sequence in (list, tuple):
    for case in ((lt, False), (le, True), (gt, False), (ge, True)):
        compare = case[0]
        empty_result = case[1]
        assert compare(make_sequence(sequence), make_sequence(sequence)) is empty_result
    for i in range(4):
        a = make_sequence(sequence, (1, 2))
        b = make_sequence(sequence, (1, 3))
        prefix = make_sequence(sequence, (1,))
        assert lt(a, b) is True
        assert le(a, b) is True
        assert gt(b, a) is True
        assert ge(b, a) is True
        assert lt(prefix, a) is True
        assert le(a, a) is True
        assert ge(a, a) is True
        assert lt(a, a) is False
        assert gt(a, a) is False
        assert le(b, a) is False
        assert ge(a, b) is False
        assert lt(make_sequence(sequence, ([], (1, 2))), make_sequence(sequence, ([], (1, 3)))) is True
        assert gt(make_sequence(sequence, ([2],)), make_sequence(sequence, ([1],))) is True
    for method in (sequence.__lt__, sequence.__le__, sequence.__gt__, sequence.__ge__):
        assert method(make_sequence(sequence), object()) is NotImplemented
        other_sequence = () if sequence is list else []
        assert method(make_sequence(sequence), other_sequence) is NotImplemented
        try:
            method(1, make_sequence(sequence))
            assert False
        except TypeError:
            pass
    for compare in operations:
        try:
            compare(make_sequence(sequence), object())
            assert False
        except TypeError:
            pass
        try:
            compare([], ())
            assert False
        except TypeError:
            pass
