# Normal Python operator dispatch, exercised on both CloverVM and CPython 3.14.5.


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


class Source(list):
    def __len__(self):
        raise AssertionError("overridden len")

    def __iter__(self):
        raise AssertionError("overridden iter")

    def __getitem__(self, index):
        raise AssertionError("overridden getitem")

    def __setitem__(self, index, value):
        raise AssertionError("overridden setitem")

    def append(self, item):
        raise AssertionError("overridden append")

    def clear(self):
        raise AssertionError("overridden clear")


source = Source([3, 1, 2])
assert source.sort() is None
assert list.__len__(source) == 3
assert list.__getitem__(source, 0) == 1
assert list.__getitem__(source, 1) == 2
assert list.__getitem__(source, 2) == 3


class Shared:
    def __eq__(self, other):
        raise AssertionError("identity must precede equality")

    def __lt__(self, other):
        raise AssertionError("identity must precede ordering inside a tuple")


shared = Shared()
rows = [(shared, 3), (shared, 1), (shared, 2)]
rows.sort()
assert rows == [(shared, 1), (shared, 2), (shared, 3)]


events = []


class Truth:
    def __init__(self, value):
        self.value = value

    def __bool__(self):
        events.append("truth")
        return self.value


class Item:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        events.append("less")
        return Truth(self.value < other.value)

    def __eq__(self, other):
        raise AssertionError("sort uses less, not equality")


items = [Item(3), Item(1), Item(2)]
items.sort()
assert items[0].value == 1
assert items[1].value == 2
assert items[2].value == 3
assert len(events) > 0
assert events.count("less") == events.count("truth")


class LengthTruth:
    def __init__(self, value):
        self.value = value

    def __len__(self):
        return self.value


class LengthItem:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return LengthTruth(1 if self.value < other.value else 0)


items = [LengthItem(2), LengthItem(1)]
items.sort()
assert items[0].value == 1


class Base:
    def __lt__(self, other):
        raise AssertionError("subclass reflection must take precedence")


class Derived(Base):
    def __gt__(self, other):
        events.append("reflected")
        return True


base = Base()
derived = Derived()
items = [derived, base]
items.sort()
assert items[0] is base
assert items[1] is derived
assert events[-1] == "reflected"


class Left:
    def __lt__(self, other):
        return NotImplemented


class Right:
    def __gt__(self, other):
        return True


left = Left()
right = Right()
items = [right, left]
items.sort()
assert items[0] is left
assert items[1] is right


# Keys may be class calls using the existing VM construction protocol.
class Key:
    def __init__(self, value):
        self.value = -value

    def __lt__(self, other):
        return self.value < other.value


items = [1, 3, 2]
items.sort(key=Key)
assert items == [3, 2, 1]


class BadTruth:
    def __bool__(self):
        return 42


class BadComparison:
    def __lt__(self, other):
        return BadTruth()


a = BadComparison()
b = BadComparison()
items = [a, b]
raises(TypeError, list.sort, items)
assert len(items) == 2
assert items.count(a) == 1
assert items.count(b) == 1


# Inconsistent comparisons need not produce a specified order, but must finish
# without losing or duplicating any original object.
mode = 0
counter = 0


class Inconsistent:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        global counter
        counter += 1
        if mode == 0:
            return True
        if mode == 1:
            return (self.value - other.value) % 3 == 1
        return counter % 2 == 0


for mode in range(3):
    for reverse in (False, True):
        original = []
        for i in range(41):
            original.append(Inconsistent(i))
        items = original.copy()
        items.sort(reverse=reverse)
        assert len(items) == len(original)
        for value in original:
            assert items.count(value) == 1


class DescendingList(list):
    def __lt__(self, other):
        return list.__gt__(self, other)


rows = [DescendingList([1]), DescendingList([3]), DescendingList([2])]
rows.sort()
assert rows == [[3], [2], [1]]


# Previously cached dispatch must honor class changes made by a callback.
changed = False


def replacement(self, other):
    events.append("replacement")
    return self.value < other.value


class Changing:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        global changed
        changed = True
        Changing.__lt__ = replacement
        return self.value < other.value


items = [Changing(4), Changing(3), Changing(2), Changing(1)]
items.sort()
assert changed
assert events.count("replacement") > 0
assert items[0].value == 1
assert items[1].value == 2
assert items[2].value == 3
assert items[3].value == 4
