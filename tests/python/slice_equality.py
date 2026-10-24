# Structural slice equality, including managed callbacks and cache invalidation.
def raises(kind, fn, *args):
    try:
        fn(*args)
    except kind:
        return
    assert False, 'expected exception'


def equal(left, right):
    return left == right


def unequal(left, right):
    return left != right


for fields in ((None, None, None), (1, 5, None), (1, 5, 2), (None, 5, 0)):
    a = slice(*fields)
    b = slice(*fields)
    assert a is not b
    assert equal(a, b) is True
    assert unequal(a, b) is False
    assert slice.__eq__(a, b) is True
    assert slice.__ne__(a, b) is False
    for other in (None, 1, fields, [1, 5, 2]):
        assert slice.__eq__(a, other) is NotImplemented
        assert slice.__ne__(a, other) is NotImplemented
        assert equal(a, other) is False
        assert unequal(a, other) is True

for method in (slice.__eq__, slice.__ne__):
    for receiver in (1, None, (), object()):
        raises(TypeError, method, receiver, receiver)
    raises(TypeError, method, slice(1))
    raises(TypeError, method, slice(1), slice(1), slice(1))

assert slice(1, 5) != slice(1, 5, 1)
assert slice(0, 5, 2) != slice(1, 5, 2)
assert slice(1, 6, 2) != slice(1, 5, 2)
assert slice(1, 5, 3) != slice(1, 5, 2)


class Bomb:
    def __eq__(self, other):
        raise ValueError('equality')

    def __ne__(self, other):
        raise AssertionError('field inequality must not run')

    def __index__(self):
        raise AssertionError('index must not run')


bomb = Bomb()
a = slice(bomb, bomb, bomb)
assert equal(a, a) is True
assert unequal(a, a) is False
assert slice.__eq__(a, a) is True
assert slice.__ne__(a, a) is False
assert equal(a, slice(bomb, bomb, bomb)) is True
for fn in (equal, unequal, slice.__eq__, slice.__ne__):
    raises(ValueError, fn, slice(Bomb()), slice(Bomb()))

nan = float('nan')
assert slice(nan) == slice(nan)
assert slice(nan) != slice(float('nan'))
shared = object()
assert slice(shared) == slice(shared)
assert slice(object()) != slice(object())
assert slice([1, {'a': (2,)}], slice(3), None) == slice([1, {'a': (2,)}], slice(3), None)
assert slice([1], slice(3), None) != slice([1], slice(4), None)

