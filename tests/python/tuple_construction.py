# Original constructor regressions, checked against CPython 3.14.5.
assert tuple() == ()
assert tuple([]) == ()
assert tuple([1, 2]) == (1, 2)
assert tuple("a\u00e9") == ("a", "\u00e9")
assert tuple(range(3)) == (0, 1, 2)
source = ([], 2)
assert tuple(source) is source
assert tuple.__new__(tuple, source) is source
assert tuple.__new__(tuple) == ()


class T(tuple):
    pass


empty = T()
assert empty.__class__ is T
assert empty == ()
assert T() is not empty
sub = T(source)
assert sub.__class__ is T
assert sub is not source
assert sub[0] is source[0]
sub.note = "attribute"
assert sub.note == "attribute"
normalized = tuple(sub)
assert normalized.__class__ is tuple
assert normalized is not sub
assert normalized == source
assert T(sub) is not sub
assert tuple.__new__(T, [3, 4]) == (3, 4)
assert tuple.__new__(T, [3]).__class__ is T


class Iterated(T):
    def __iter__(self):
        return iter([7, 8])

    def __len__(self):
        raise AssertionError("payload length override must not be called")

    def __getitem__(self, index):
        raise AssertionError("payload indexing override must not be called")


sub = Iterated([1, 2])
assert tuple(sub) == (7, 8)
assert T(sub) == (7, 8)


class Iterator:
    def __init__(self, values):
        self.values = values
        self.index = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.index == len(self.values):
            raise StopIteration
        item = self.values[self.index]
        self.index += 1
        return item


iterator = Iterator([1, 2])
assert tuple(iterator) == (1, 2)
assert tuple(iterator) == ()
assert T(Iterator([3, 4])) == (3, 4)

# Iteration observes mutations rather than copying an initial source snapshot.
values = [1, 2]


class Mutating(Iterator):
    def __next__(self):
        if self.index == 0:
            self.values.append(3)
        return Iterator.__next__(self)


assert tuple(Mutating(values)) == (1, 2, 3)


class Raising(Iterator):
    def __next__(self):
        if self.index == 1:
            raise ValueError("iteration failed")
        return Iterator.__next__(self)


for cls in (tuple, T):
    try:
        cls(Raising([[1], [2]]))
    except ValueError as error:
        assert str(error) == "iteration failed"
    else:
        assert False
    assert cls([9]) == (9,)


class BadIter:
    def __iter__(self):
        raise KeyError("iter failed")


try:
    tuple(BadIter())
except KeyError as error:
    assert error.args == ("iter failed",)
else:
    assert False

# Receiver validation precedes source iteration.
observations = []


class Observed:
    def __iter__(self):
        observations.append(1)
        return iter([1])


for invalid in (None, 1, [], list):
    try:
        tuple.__new__(invalid, Observed())
    except TypeError:
        pass
    else:
        assert False
assert observations == []

for cls in (tuple, T):
    for case in (0, 1, 2, 3):
        try:
            if case == 0:
                cls([1], [2])
            elif case == 1:
                cls(iterable=[1])
            elif case == 2:
                cls(None)
            else:
                cls(1)
        except TypeError:
            pass
        else:
            assert False

try:
    tuple.__new__()
except TypeError:
    pass
else:
    assert False
try:
    tuple.__new__(tuple, [1], [2])
except TypeError:
    pass
else:
    assert False
try:
    tuple.__new__(tuple, iterable=[1])
except TypeError:
    pass
else:
    assert False


class NewOnly(tuple):
    def __new__(cls, value=5):
        return tuple.__new__(cls, [value])


assert NewOnly() == (5,)
assert NewOnly(value=6) == (6,)
assert NewOnly(7).__class__ is NewOnly


class Unrelated(tuple):
    def __new__(cls):
        return 42


assert Unrelated() == 42

# Repeated calls exercise the class-call cache before changing inherited __new__.
class Base(tuple):
    pass


class Child(Base):
    pass


def construct():
    return Child([1, 2])


assert construct() == (1, 2)
assert construct() == (1, 2)


def replacement(cls, values):
    return tuple.__new__(cls, [9])


Base.__new__ = replacement
assert construct() == (9,)
del Base.__new__
assert construct() == (1, 2)

# Conversion uses runtime-owned primitives rather than public builtin names.
constructor = tuple
tuple = None
list = None
assert constructor([3, 4]) == (3, 4)
assert T([5]) == (5,)
tuple = constructor
del list
