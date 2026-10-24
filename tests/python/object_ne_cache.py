class Equal:
    def __eq__(self, other):
        return self.result
a = Equal()
b = Equal()
def direct(x, y):
    return object.__ne__(x, y)
def operator(x, y):
    return x != y
# Warm the same eq/ne/truth sites, then change/delete class methods.
class Base:
    def __eq__(self, other):
        return False
class Mutable(Base):
    def __eq__(self, other):
        return []
class MutableTruth:
    def __bool__(self):
        return False
x = Mutable()
y = Mutable()
for i in range(100):
    assert direct(x, y) is True
    assert operator(x, y) is True
    assert (x == y) == []
def true_eq(self, other):
    return True
Mutable.__eq__ = true_eq
for i in range(100):
    assert direct(x, y) is False
    assert operator(x, y) is False
    assert (x == y) is True
del Mutable.__eq__
assert direct(x, y) is True
assert operator(x, y) is True
truth = MutableTruth()
def truth_eq(self, other):
    return truth
Mutable.__eq__ = truth_eq
for i in range(100):
    assert direct(x, y) is True
    assert operator(x, y) is True
def true_bool(self):
    return True
MutableTruth.__bool__ = true_bool
assert direct(x, y) is False
assert operator(x, y) is False
del MutableTruth.__bool__
assert direct(x, y) is False
assert operator(x, y) is False
def zero_len(self):
    return 0
MutableTruth.__len__ = zero_len
assert direct(x, y) is True
assert operator(x, y) is True
del MutableTruth.__len__
assert direct(x, y) is False
assert operator(x, y) is False
def custom_ne(self, other):
    return 'custom ne'
Mutable.__ne__ = custom_ne
assert operator(x, y) == 'custom ne'
del Mutable.__ne__
assert operator(x, y) is False

class LenFallback:
    def __bool__(self):
        return False
    def __len__(self):
        return 5
truth = LenFallback()
a.result = truth
for i in range(100):
    assert direct(a, b) is True
    assert operator(a, b) is True
del LenFallback.__bool__
assert direct(a, b) is False
assert operator(a, b) is False
