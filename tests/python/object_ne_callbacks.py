def direct(x, y):
    return object.__ne__(x, y)
def operator(x, y):
    return x != y
class Base:
    def __eq__(self, other):
        return False
def true_eq(self, other):
    return True
# Mutation/reentry during eq and truth must retain all live operands/results.
class Reentrant:
    pass
class EphemeralTruth:
    def __init__(self, left, right):
        self.left = left
        self.right = right
    def __bool__(self):
        assert direct(object(), object()) is NotImplemented
        assert operator(object(), object()) is True
        assert self.left.marker == 'self'
        assert self.right.marker == 'other'
        Reentrant.__eq__ = true_eq
        return False
for compare in [direct, operator]:
    # Restore after mutation by the previous truth callback.
    def reentrant_eq(self, other):
        global owners
        owners = []
        assert self.marker == 'self'
        assert other.marker == 'other'
        return EphemeralTruth(self, other)
    Reentrant.__eq__ = reentrant_eq
    owners = [Reentrant(), Reentrant()]
    owners[0].marker = 'self'
    owners[1].marker = 'other'
    assert compare(owners[0], owners[1]) is True
    assert owners == []

# Payload-specific builtin __ne__ keeps its established inherited behavior.
class ListEqual(list):
    def __eq__(self, other):
        return False
assert (ListEqual([1]) != [1]) is False

# Mutating the active eq method affects the next call, while this invocation
# still truth-converts its own result. Reentry uses an independent live frame.
class MutatesEq(Base):
    def __eq__(self, other):
        del MutatesEq.__eq__
        assert direct(self, other) is True
        return [1]
x = MutatesEq()
y = MutatesEq()
assert direct(x, y) is False
assert direct(x, y) is True

def mutates_eq(self, other):
    MutatesEq.__eq__ = true_eq
    assert operator(self, other) is False
    return []
MutatesEq.__eq__ = mutates_eq
assert operator(x, y) is True
assert operator(x, y) is False
