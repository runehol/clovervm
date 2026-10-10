# List and tuple searches use identity followed by Python equality.

def raises(error, function, *args):
    try:
        # Clover supports collecting varargs, but not starred call arguments.
        if len(args) == 0:
            function()
        elif len(args) == 1:
            function(args[0])
        elif len(args) == 2:
            function(args[0], args[1])
        elif len(args) == 3:
            function(args[0], args[1], args[2])
        else:
            function(args[0], args[1], args[2], args[3])
    except error:
        return
    assert False


def sequence_class(values):
    if isinstance(values, list):
        return list
    return tuple


for values in ([1.0, True, 2.0, 1], (1.0, True, 2.0, 1)):
    assert values.count(1) == 3
    assert values.index(1) == 0
    assert values.index(1, 1) == 1
    assert values.index(1, 2) == 3
    assert values.index(1, -1) == 3
    assert values.index(1, -(10**30), 10**30) == 0
    assert values.index(1, True) == 1
    assert isinstance(values.index(1, True), int)
    assert not isinstance(values.index(1, True), bool)
    assert values.count(9) == 0
    raises(ValueError, sequence_class(values).index, values, 1, 10**30)
    raises(ValueError, sequence_class(values).index, values, 1, 0, -(10**30))
    raises(ValueError, sequence_class(values).index, values, 1, 2, 3)
    raises(ValueError, sequence_class(values).index, values, 9)
    raises(TypeError, sequence_class(values).index, values, 1, 0.0)
    raises(TypeError, sequence_class(values).index, values, 1, 0, None)

for values in ([], ()):
    assert values.count(1) == 0
    raises(TypeError, sequence_class(values).index, values, 1, 'bad')
    raises(ValueError, sequence_class(values).index, values, 1)

for method in (list.count, list.index, list.remove, tuple.count, tuple.index):
    raises(TypeError, method, 1, 1)
    raises(TypeError, method)
    raises(TypeError, method, [])

raises(TypeError, list.count, (), 1)
raises(TypeError, tuple.count, [], 1)

values = [1.0, 2, 1]
assert values.remove(1) is None
assert len(values) == 2
assert values[0] == 2
assert values[1] == 1
raises(ValueError, list.remove, values, 9)

import math
nan = math.nan
for values in ([nan, 1], (nan, 1)):
    assert values.count(nan) == 1
    assert values.index(nan) == 0


class NoCompare:
    def __eq__(self, other):
        raise AssertionError


same = NoCompare()
assert [same].count(same) == 1
assert (same,).index(same) == 0
assert [same].remove(same) is None


class Equal:
    def __eq__(self, other):
        return other == 7


for values in ([Equal()], (Equal(),)):
    assert values.count(7) == 1
    assert values.index(7) == 0


class Left:
    def __eq__(self, other):
        return NotImplemented


class Right:
    def __eq__(self, other):
        return True


for values in ([Left()], (Left(),)):
    assert values.count(Right()) == 1
    assert values.index(Right()) == 0


class TruthEqual:
    def __eq__(self, other):
        # Custom __bool__ dispatch is not yet supported; test builtin truth.
        return 1


assert [TruthEqual()].count(0) == 1
assert (TruthEqual(),).index(0) == 0


class Failing:
    def __eq__(self, other):
        raise ValueError


for values in ([Failing()], (Failing(),)):
    raises(ValueError, sequence_class(values).count, values, 0)
    raises(ValueError, sequence_class(values).index, values, 0)
values = [Failing()]
raises(ValueError, list.remove, values, 0)
assert len(values) == 1


class Append:
    def __eq__(self, other):
        values.append(other)
        return False


values = [Append()]
assert values.count(7) == 1
values = [Append()]
assert values.index(7) == 1
values = [Append()]
raises(ValueError, sequence_class(values).index, values, 7, 0, 1)
values = [Append()]
values.remove(7)
assert len(values) == 1


class Clear:
    def __init__(self, result):
        self.result = result

    def __eq__(self, other):
        values.clear()
        return self.result


values = [Clear(True)]
assert values.count(7) == 1
values = [Clear(False)]
assert values.count(7) == 0
values = [Clear(True)]
assert values.index(7) == 0
values = [Clear(False)]
raises(ValueError, sequence_class(values).index, values, 7)
values = [Clear(True)]
assert values.remove(7) is None
assert len(values) == 0
values = [Clear(False)]
raises(ValueError, list.remove, values, 7)


class Replace:
    def __eq__(self, other):
        values.clear()
        values.append(8)
        return True


values = [Replace()]
assert values.remove(7) is None
assert len(values) == 0

# Cache replay must observe replacement and deletion of equality methods.
class Cached:
    def __eq__(self, other):
        return True


cached = Cached()
for values in ([cached], (cached,)):
    assert values.count(0) == 1
    assert values.count(0) == 1
    assert values.index(0) == 0
    assert values.index(0) == 0


def unequal(self, other):
    return False


Cached.__eq__ = unequal
for values in ([cached], (cached,)):
    assert values.count(0) == 0
    raises(ValueError, sequence_class(values).index, values, 0)

del Cached.__eq__
for values in ([cached], (cached,)):
    assert values.count(0) == 0
    assert values.count(cached) == 1

# These builtin methods take positional arguments only.
def list_count_keyword():
    [1].count(needle=1)


def list_index_keyword():
    [1].index(1, start=0)


def list_remove_keyword():
    [1].remove(needle=1)


def tuple_count_keyword():
    (1,).count(needle=1)


def tuple_index_keyword():
    (1,).index(1, stop=1)


for function in (list_count_keyword, list_index_keyword, list_remove_keyword,
                 tuple_count_keyword, tuple_index_keyword):
    raises(TypeError, function)
