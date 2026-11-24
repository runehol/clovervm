from functools import cmp_to_key
from operator import eq, ge, gt, le, lt, ne


def expect_error(expected, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except expected as error:
        return error
    assert False


def ascending(left, right):
    return left - right


class Descending:
    def compare(self, left, right):
        return right - left

    def __call__(self, left, right):
        return self.compare(left, right)


class ConstructorComparator:
    def __new__(cls, left, right):
        return left - right


factory = cmp_to_key(mycmp=ascending)
descending = Descending()
saved = descending.compare
native_saved = "xxx".find


class Owner:
    adapter = cmp_to_key
    key = factory


assert Owner.adapter(ascending)(1).obj == 1
assert Owner().adapter(mycmp=ascending)(obj=2).obj == 2
assert Owner.key(3).obj == 3
assert Owner().key(obj=4).obj == 4

for comparator in (ascending, ConstructorComparator):
    assert sorted([3, 1, 2], key=cmp_to_key(comparator)) == [1, 2, 3]
for comparator in (descending, saved):
    values = [1, 3, 2]
    values.sort(key=cmp_to_key(comparator))
    assert values == [3, 2, 1]
native_factory = cmp_to_key(native_saved)
assert native_factory("x") > native_factory(1)


def check_binding(function, keyword, arg):
    expect_error(TypeError, function)
    expect_error(TypeError, function, arg, arg)
    expect_error(TypeError, function, wrong=arg)
    expect_error(TypeError, function, arg, **{keyword: arg})
    expect_error(TypeError, function, **{keyword: arg, "wrong": arg})


check_binding(cmp_to_key, "mycmp", ascending)
check_binding(factory, "obj", 1)

left = factory(1)
right = factory(2)
assert left is not factory(1)
assert left.__class__ is factory.__class__
assert left(obj=3).obj == 3
assert left.obj == 1
assert left < right
assert left <= right
assert left == factory(1)
assert left != right
assert right > left
assert right >= left
assert left < cmp_to_key(descending)(2)
assert not (cmp_to_key(descending)(1) < right)

for key in (factory, left):
    assert key.__hash__ is None
    expect_error(TypeError, hash, key)

for operation in (lt, le, eq, ne, gt, ge):
    expect_error(TypeError, operation, left, 1)
    expect_error(TypeError, operation, 1, left)
    expect_error(AttributeError, operation, factory, left)
    expect_error(AttributeError, operation, left, factory)

assert factory.obj is None
factory.obj = 0
assert factory < left
del factory.obj
del factory.obj
assert factory.obj is None
left.obj = 5
assert left > right
del left.obj
assert left.obj is None
expect_error(AttributeError, eq, left, right)


def both_none(left, right):
    assert left is None
    assert right is None
    return 0


none_factory = cmp_to_key(both_none)
none_factory.obj = None
assert none_factory == none_factory(None)


class RawResult:
    def __lt__(self, other):
        return ("lt", other)

    def __le__(self, other):
        return ("le", other)

    def __eq__(self, other):
        return ("eq", other)

    def __ne__(self, other):
        return ("ne", other)

    def __gt__(self, other):
        return ("gt", other)

    def __ge__(self, other):
        return ("ge", other)


calls = []


def compare_to_raw(left, right):
    calls.append((left, right))
    return RawResult()


raw_key = cmp_to_key(compare_to_raw)(1)
assert calls == []
for case in ((lt, "lt"), (le, "le"), (eq, "eq"), (ne, "ne"), (gt, "gt"), (ge, "ge")):
    operation = case[0]
    name = case[1]
    assert operation(raw_key, right) == (name, 0)
assert len(calls) == 6
for pair in calls:
    assert pair == (1, 2)
assert (raw_key == raw_key) == ("eq", 0)
assert calls[-1] == (1, 1)


failure = ValueError("comparison failed")


def fail(left, right):
    raise failure


class FailingResult:
    def __lt__(self, other):
        raise failure


def bad_result(left, right):
    return FailingResult()


for comparator in (fail, bad_result):
    assert expect_error(ValueError, lt, cmp_to_key(comparator)(0), right) is failure

non_callable = cmp_to_key(None)
assert non_callable(0).obj == 0
expect_error(TypeError, lt, non_callable(0), right)
assert right > factory(1)
