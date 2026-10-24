# CPython 3.14.5 Objects/sliceobject.c:slice_richcompare and
# Objects/tupleobject.c:tuple_richcompare are the semantic reference.
def raises(kind, fn, *args):
    try:
        fn(*args)
    except kind:
        return
    assert False, 'expected exception'


def lt(a, b):
    return a < b


def le(a, b):
    return a <= b


def gt(a, b):
    return a > b


def ge(a, b):
    return a >= b


class Bomb:
    def __eq__(self, other):
        raise AssertionError('identity must bypass equality')

    def __lt__(self, other):
        raise AssertionError('identity must bypass ordering')

    def __index__(self):
        raise AssertionError('comparison must not normalize')


def test_slice_ordering():
    methods = (slice.__lt__, slice.__le__, slice.__gt__, slice.__ge__)
    operators = (lt, le, gt, ge)
    # Both native shapes, cross-shape comparisons, and each decisive position.
    for pair in ((slice(1, 3), slice(2, 4)),
                 (slice(1, 3, 1), slice(2, 4, 1)),
                 (slice(1, 3), slice(2, 4, 1)),
                 (slice(1, 3, 1), slice(1, 4, 1)),
                 (slice(1, 3, 0), slice(1, 3, 1))):
        a = pair[0]
        b = pair[1]
        for i in range(4):
            expected = i < 2
            assert operators[i](a, b) is expected
            assert methods[i](a, b) is expected
            assert operators[i](b, a) is (not expected)

    for fields in ((None, None, None), (1, 3, None), (1, 3, 1)):
        a = slice(*fields)
        b = slice(*fields)
        for i in range(4):
            expected = i == 1 or i == 3
            assert operators[i](a, b) is expected
            assert operators[i](a, a) is expected
            assert methods[i](a, b) is expected
            assert methods[i](a, a) is expected

    for i in range(4):
        for receiver in (None, 1, (), object()):
            raises(TypeError, methods[i], receiver, slice(1))
        raises(TypeError, methods[i], slice(1))
        raises(TypeError, methods[i], slice(1), slice(2), slice(3))
        for peer in (None, 1, (), object()):
            assert methods[i](slice(1), peer) is NotImplemented
            raises(TypeError, operators[i], slice(1), peer)
        for pair in ((slice(None), slice(1)),
                     (slice(1, 3), slice(1, 3, 1)),
                     (slice(object()), slice(object()))):
            raises(TypeError, operators[i], pair[0], pair[1])


    bomb = Bomb()
    for i in range(4):
        a = slice(bomb, bomb, bomb)
        assert operators[i](a, a) is (i == 1 or i == 3)
        assert methods[i](a, slice(bomb, bomb, bomb)) is (i == 1 or i == 3)
    assert slice(bomb, 1) < slice(bomb, 2)

    nan = float('nan')
    for i in range(4):
        assert operators[i](slice(nan), slice(nan)) is (i == 1 or i == 3)
        assert operators[i](slice(float('nan')), slice(float('nan'))) is False
    assert slice(slice(1)) < slice(slice(2))
    assert slice((1, 2)) < slice((1, 3))
    assert slice((1, 2)) <= slice((1, 2))
    assert slice(slice(2)) > slice(slice(1))
    assert slice((1, 3)) >= slice((1, 2))


test_slice_ordering()
