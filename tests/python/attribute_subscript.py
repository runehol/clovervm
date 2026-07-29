class Box:
    pass

obj = Box()
obj.value = 7
lst = [obj]
assert lst[0].value == 7

lst[0].value = 11
assert obj.value == 11

lst[0].value += 5
assert obj.value == 16

obj.lst = [4, 7, 9]
assert obj.lst[1] == 7

obj.lst[1] = 11
assert obj.lst[1] == 11

obj.lst[1] += 5
assert obj.lst[1] == 16

slotdict = obj.__dict__
assert slotdict.__getitem__("value") == slotdict["value"]
assert slotdict.__getitem__("lst") == slotdict["lst"]


# User-defined subscript methods run on every access and observe method replacement.
class Bag:
    def __init__(self):
        self.count = 0
        self.total = 0

    def __getitem__(self, key):
        self.count += 1
        return key + self.count

    def __setitem__(self, key, value):
        self.total = key * 10 + value

    def __delitem__(self, key):
        self.total = key + 5


bag = Bag()
assert bag[10] == 11
assert bag[10] == 12
bag[4] = 7
assert bag.total == 47
del bag[4]
assert bag.total == 9


class NotImplementedBag:
    def __getitem__(self, key):
        return NotImplemented


assert NotImplementedBag()[0] is NotImplemented


class ReplaceableBag:
    def __getitem__(self, key):
        return 1

    def __setitem__(self, key, value):
        self.total = 1

    def __delitem__(self, key):
        self.total = 1


replaceable_bag = ReplaceableBag()
assert replaceable_bag[0] == 1


def replacement_getitem(self, key):
    return 2


ReplaceableBag.__getitem__ = replacement_getitem
assert replaceable_bag[0] == 2

replaceable_bag[0] = 0
assert replaceable_bag.total == 1


def replacement_setitem(self, key, value):
    self.total = 2


ReplaceableBag.__setitem__ = replacement_setitem
replaceable_bag[0] = 0
assert replaceable_bag.total == 2

del replaceable_bag[0]
assert replaceable_bag.total == 1


def replacement_delitem(self, key):
    self.total = 2


ReplaceableBag.__delitem__ = replacement_delitem
del replaceable_bag[0]
assert replaceable_bag.total == 2


class BaseBag:
    def __getitem__(self, key):
        return 1


class InheritedBag(BaseBag):
    pass


inherited_bag = InheritedBag()
assert inherited_bag[0] == 1
BaseBag.__getitem__ = replacement_getitem
assert inherited_bag[0] == 2

# Subscript assignment evaluates the right-hand side before its target and slice bounds.
rhs_order_list = [0, 0]
rhs_order_index = 0


def subscript_rhs():
    global rhs_order_index
    rhs_order_index = 1
    return 7


rhs_order_list[rhs_order_index] = subscript_rhs()
assert rhs_order_list[0] * 10 + rhs_order_list[1] == 7

rhs_order_list = [0, 0]
rhs_order_index = 0
rhs_order_list[rhs_order_index]: int = subscript_rhs()
assert rhs_order_list[0] * 10 + rhs_order_list[1] == 7

slice_order = 0


def mark_slice_order(expected):
    global slice_order
    if slice_order == expected:
        slice_order += 1
        return 1
    return 100


def slice_rhs():
    return mark_slice_order(0)


def make_slice_bag():
    mark_slice_order(1)
    return SliceBag()


def slice_start():
    return mark_slice_order(2)


def slice_stop():
    return mark_slice_order(3)


class SliceBag:
    def __setitem__(self, key, value):
        global slice_order
        if (
            value == 1
            and key.start == 1
            and key.stop == 1
            and key.step is None
            and slice_order == 4
        ):
            slice_order = 9


make_slice_bag()[slice_start():slice_stop()] = slice_rhs()
assert slice_order == 9

# Builtin sequences accept boolean indices.
assert [1, 2][True] == 2
assert (1, 2)[False] == 1
assert "ab"[True] == "b"

# List, tuple, and string slicing honors positive and negative bounds and steps.
assert repr([1, 2, 3, 4][1:3]) == "[2, 3]"
assert repr([1, 2, 3, 4][0:-1]) == "[1, 2, 3]"
assert repr([1, 2, 3, 4][::2]) == "[1, 3]"
assert repr([1, 2, 3, 4][::-1]) == "[4, 3, 2, 1]"
assert repr([1, 2, 3, 4][3:0:-1]) == "[4, 3, 2]"

assert repr((1, 2, 3, 4)[1:3]) == "(2, 3)"
assert repr((1, 2, 3, 4)[0:-1]) == "(1, 2, 3)"
assert repr((1, 2, 3, 4)[::2]) == "(1, 3)"
assert repr((1, 2, 3, 4)[::-1]) == "(4, 3, 2, 1)"
assert repr((1, 2, 3, 4)[3:0:-1]) == "(4, 3, 2)"

assert "abcd"[1:3] == "bc"
assert "abcd"[0:-1] == "abc"
assert "abcd"[::2] == "ac"
assert "abcd"[::-1] == "dcba"
assert "abcd"[3:0:-1] == "dcb"


# Failed or missing __getitem__ lookups do not poison later cached accesses.
class InitiallyNotSubscriptable:
    pass


def dynamic_get(obj, key):
    return obj[key]


dynamic_bag = InitiallyNotSubscriptable()
try:
    dynamic_get(dynamic_bag, 0)
    negative_lookup_raised = False
except TypeError:
    negative_lookup_raised = True


def installed_getitem(self, key):
    return key + 9


InitiallyNotSubscriptable.__getitem__ = installed_getitem
assert negative_lookup_raised
assert dynamic_get(dynamic_bag, 3) == 12


class RaisingThenReturningBag:
    def __init__(self):
        self.count = 0

    def __getitem__(self, key):
        self.count += 1
        if key == 1:
            raise ValueError
        return key + self.count * 10


raising_bag = RaisingThenReturningBag()
assert dynamic_get(raising_bag, 0) == 10
try:
    dynamic_get(raising_bag, 1)
    raised_lookup_caught = False
except ValueError:
    raised_lookup_caught = True
assert raised_lookup_caught
assert dynamic_get(raising_bag, 2) == 32

# Slice repr exposes all fields, and indices normalizes bounds for a sequence length.
assert repr(slice(1, 2, None)) == "slice(1, 2, None)"


def assert_slice_indices(value, length, start, stop, step):
    normalized = value.indices(length)
    assert normalized[0] == start
    assert normalized[1] == stop
    assert normalized[2] == step


assert_slice_indices(slice(None, None), 5, 0, 5, 1)
assert_slice_indices(slice(None, None, -1), 5, 4, -1, -1)
assert_slice_indices(slice(0, -1), 5, 0, 4, 1)
assert_slice_indices(slice(-10, 10), 5, 0, 5, 1)
assert_slice_indices(slice(10, -10, -2), 5, 4, -1, -2)
assert_slice_indices(slice(1, 8, 2), 10, 1, 8, 2)
