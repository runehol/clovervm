# Slice comparison cache invalidation, mutation, and owner lifetime.
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


# Warm the same sites, then replace/delete equality and truth methods.
class MutableTruth:
    def __bool__(self):
        return True


class MutableField:
    def __eq__(self, other):
        return MutableTruth()


a = slice(MutableField(), 9)
b = slice(MutableField(), 9)
for i in range(100):
    assert equal(a, b) and not unequal(a, b)


def false_bool(self):
    return False


MutableTruth.__bool__ = false_bool
for i in range(100):
    assert not equal(a, b) and unequal(a, b)
del MutableTruth.__bool__
for i in range(100):
    assert equal(a, b) and not unequal(a, b)


def false_eq(self, other):
    return False


MutableField.__eq__ = false_eq
for i in range(100):
    assert not equal(a, b) and unequal(a, b)
del MutableField.__eq__
for i in range(100):
    assert not equal(a, b) and unequal(a, b)

# Callbacks may mutate contained values, reenter comparison, and drop the owners.
owners = []
mutated_left = []
mutated_right = [7]


class Reenter:
    def __eq__(self, other):
        owners.clear()
        mutated_left.append(7)
        assert slice([1], slice(2)) == slice([1], slice(2))
        # Allocate while only the managed comparison retains slices/field tuples.
        junk = []
        for i in range(50):
            junk.append(list(range(20)))
        assert len(junk) == 50
        return True


owners.append(slice(Reenter(), mutated_left, None))
owners.append(slice(Reenter(), mutated_right, None))
assert equal(owners[0], owners[1])
assert owners == []
assert mutated_left == [7]

# Mutating a later field's class is observed within the same comparison.
class Later:
    def __eq__(self, other):
        return False


def true_later_eq(self, other):
    return True


class Earlier:
    def __eq__(self, other):
        Later.__eq__ = true_later_eq
        return True


assert slice(Earlier(), Later(), None) == slice(Earlier(), Later(), None)

# Truth exceptions propagate through both slice methods.
class RaisingTruth:
    def __bool__(self):
        raise RuntimeError('bool callback')


class RaisingTruthField:
    def __eq__(self, other):
        return RaisingTruth()


for fn in (equal, unequal, slice.__eq__, slice.__ne__):
    raises(RuntimeError, fn, slice(RaisingTruthField()), slice(RaisingTruthField()))

# Outer dispatch may continue to a non-slice operand after NotImplemented.
class Other:
    def __eq__(self, other):
        return 'outer eq'

    def __ne__(self, other):
        return 'outer ne'


assert equal(slice(1), Other()) == 'outer eq'
assert unequal(slice(1), Other()) == 'outer ne'
