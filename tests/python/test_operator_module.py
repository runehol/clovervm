# Python test set -- operator module
#
# Surface selected from Python 3.14's operator module help. This file does not
# copy CPython test code.

import operator


assert operator.add(2, 5) == 7
assert operator.__add__(2, 5) == 7
assert operator.sub(9, 4) == 5
assert operator.mul(6, 7) == 42
assert operator.floordiv(8, 3) == 2
assert operator.mod(8, 3) == 2
assert operator.truediv(7, 2) == 3.5
assert operator.lshift(3, 2) == 12
assert operator.rshift(9, 1) == 4

assert operator.neg(4) == -4
assert operator.pos(-4) == -4
assert operator.abs(-4) == 4
assert operator.abs(4) == 4
assert operator.not_(False)
assert not operator.not_(True)
assert operator.truth(1)
assert not operator.truth(0)

assert operator.eq("a", "a")
assert operator.ne("a", "b")
assert operator.lt(2, 3)
assert operator.le(3, 3)
assert operator.gt(4, 3)
assert operator.ge(4, 4)
assert operator.is_(None, None)
assert operator.is_not(None, 0)
assert operator.is_none(None)
assert operator.is_not_none(0)

items = [4, 7, 9]
assert operator.getitem(items, 1) == 7
operator.setitem(items, 1, 11)
assert items[1] == 11
operator.delitem(items, 1)
assert items[1] == 9

mapping = {"alpha": 4}
assert operator.getitem(mapping, "alpha") == 4
operator.setitem(mapping, "beta", 7)
assert mapping["beta"] == 7
operator.delitem(mapping, "alpha")
assert mapping.get("alpha") is None

assert operator.concat((1, 2), (3,))[2] == 3
assert operator.iconcat([1], [2])[1] == 2
assert operator.iadd(2, 3) == 5
assert operator.isub(9, 2) == 7
assert operator.imul(3, 4) == 12
assert operator.ifloordiv(8, 3) == 2
assert operator.imod(8, 3) == 2
assert operator.itruediv(7, 2) == 3.5
assert operator.ilshift(1, 4) == 16
assert operator.irshift(16, 2) == 4

assert operator.contains([1, 2, 3], 2)
assert not operator.contains([1, 2, 3], 5)
assert operator.countOf([1, 2, 1, 3], 1) == 2
assert operator.indexOf(["a", "b", "c"], "b") == 1
assert operator.length_hint([1, 2, 3]) == 3
assert operator.length_hint(42, 9) == 9
assert operator.index(12) == 12


def constant():
    return 37


def add3(a, b, c):
    return a + b + c


assert operator.call(constant) == 37
assert operator.call(add3, 1, 2, 3) == 6
assert operator.__all__[0] == "abs"
