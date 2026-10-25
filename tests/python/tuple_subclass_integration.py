# Original integration regressions checked against CPython 3.14.5.
class Payload(tuple):
    def __len__(self):
        raise AssertionError("stored length expected")

    def __getitem__(self, index):
        raise AssertionError("stored item expected")


class Child(Payload):
    pass


def eq(left, right):
    return left == right


def ne(left, right):
    return left != right


def lt(left, right):
    return left < right


for i in range(3):
    assert eq(Child((1, 2)), (1, 2)) is True
    assert eq((1, 2), Child((1, 3))) is False
    assert ne(Child((1,)), Payload((1, 2))) is True
    assert lt(Child((1,)), Payload((2,))) is True
    assert tuple.__eq__(Child((1,)), (1,)) is True
    assert tuple.__eq__(Child(), []) is NotImplemented
    assert tuple.__lt__(Child(), []) is NotImplemented


# Inherited __ne__ uses tuple payloads even with a subclass __eq__ override.
class Override(Child):
    def __eq__(self, other):
        return "override"


assert eq((), Override()) == "override"
assert ne(Override((1,)), (1,)) is False
assert ne(Override((1,)), (2,)) is True
assert tuple.__eq__(Override((1,)), (1,)) is True


def replacement(self, other):
    return "replacement"


for i in range(3):
    assert eq(Child((1,)), (1,)) is True
Payload.__eq__ = replacement
assert eq(Child((1,)), (1,)) == "replacement"
assert eq((1,), Child((1,))) == "replacement"
del Payload.__eq__
assert eq(Child((1,)), (1,)) is True


# Identity short-circuits element equality, including nonreflexive values.
class IdentityOnly:
    def __eq__(self, other):
        raise AssertionError("identity must bypass equality")


same = IdentityOnly()
assert eq(Child((same,)), Payload((same,))) is True
nan = float("nan")
assert eq(Child((nan,)), (nan,)) is True


class Reverse(Child):
    def __lt__(self, other):
        return tuple.__getitem__(self, 0) > tuple.__getitem__(other, 0)


first = Reverse((2, "first"))
second = Reverse((2, "second"))
third = Reverse((1, "third"))
source = [third, first, second]
result = sorted(source)
assert result[0] is first
assert result[1] is second
assert result[2] is third
assert source[0] is third
list.sort(source)
assert source[0] is first
assert source[1] is second
assert source[2] is third


# Drop the only external container references and reenter the same comparison
# sites while managed/native comparison continuations retain their operands.
left = None
right = None


class Element:
    def __init__(self, marker):
        self.marker = marker

    def __eq__(self, other):
        global left, right
        left = None
        right = None
        garbage = []
        for i in range(30):
            garbage.append(Child(([i],)))
        assert eq(Child((1,)), Payload((1,))) is True
        assert self.marker == 7
        assert other.marker == 8
        return False

    def __lt__(self, other):
        global left, right
        left = None
        right = None
        garbage = []
        for i in range(30):
            garbage.append(Child(([i],)))
        assert lt(Child((1,)), Payload((2,))) is True
        assert self.marker == 7
        assert other.marker == 8
        return self


left = Child((Element(7),))
right = Payload((Element(8),))
assert eq(left, right) is False
assert left is None
assert right is None
left = Child((Element(7),))
right = Payload((Element(8),))
assert lt(left, right).marker == 7
assert left is None
assert right is None


class ComparisonError(Exception):
    pass


class Raising(Child):
    def __lt__(self, other):
        raise ComparisonError("tuple sorting")


source = [Raising((2,)), Raising((1,))]
try:
    list.sort(source)
except ComparisonError as error:
    assert str(error) == "tuple sorting"
else:
    assert False
assert len(source) == 2
assert sorted([Child((2,)), Child((1,))]) == [(1,), (2,)]
