# Python test set -- operator module
#
# Surface selected from Python 3.14's operator module help. This file does not
# copy CPython test code.

import operator
import math


class NeverEqual:
    def __eq__(self, other):
        return False


class RaisingEqual:
    def __eq__(self, other):
        raise ValueError


for item in (math.nan, NeverEqual(), RaisingEqual()):
    assert operator.countOf([item, item], item) == 2
    assert operator.indexOf([item], item) == 0
    assert operator.countOf((item, item), item) == 2
    assert operator.indexOf((item,), item) == 0

item = NeverEqual()
assert operator.indexOf([NeverEqual(), item], item) == 1

assert operator.countOf([NeverEqual()], NeverEqual()) == 0
raised = False
try:
    operator.indexOf([NeverEqual()], NeverEqual())
except ValueError:
    raised = True
assert raised

for function in (operator.countOf, operator.indexOf):
    raised = False
    try:
        function([RaisingEqual()], RaisingEqual())
    except ValueError:
        raised = True
    assert raised


assert operator.add(2, 5) == 7
assert operator.__add__(2, 5) == 7
assert operator.sub(9, 4) == 5
assert operator.mul(6, 7) == 42
assert operator.floordiv(8, 3) == 2
assert operator.mod(8, 3) == 2
assert operator.pow(2, 5) == 32
assert operator.truediv(7, 2) == 3.5
assert operator.and_(6, 3) == 2
assert operator.or_(4, 1) == 5
assert operator.xor(6, 3) == 5
assert operator.lshift(3, 2) == 12
assert operator.rshift(9, 1) == 4

assert operator.neg(4) == -4
assert operator.pos(-4) == -4
assert operator.abs(-4) == 4
assert operator.abs(4) == 4
assert operator.inv(6) == -7
assert operator.invert(6) == -7
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
assert operator.ipow(2, 4) == 16
assert operator.itruediv(7, 2) == 3.5
assert operator.iand(6, 3) == 2
assert operator.ior(4, 1) == 5
assert operator.ixor(6, 3) == 5
assert operator.ilshift(1, 4) == 16
assert operator.irshift(16, 2) == 4

assert operator.contains([1, 2, 3], 2)
assert not operator.contains([1, 2, 3], 5)
assert operator.contains({"alpha": 1}, "alpha")
assert operator.countOf([1, 2, 1, 3], 1) == 2
assert operator.indexOf(["a", "b", "c"], "b") == 1
assert operator.length_hint([1, 2, 3]) == 3
assert operator.length_hint(42, 9) == 9
for default in (True, False, -1, 10**18):
    assert operator.length_hint([1], default) == 1
    result = operator.length_hint(42, default)
    assert result == int(default)
    assert result.__class__ is int

for default in (1.5, "2", None, NeverEqual()):
    for value in ([1], 42):
        raised = False
        try:
            operator.length_hint(value, default)
        except TypeError:
            raised = True
        assert raised


class RaisingLength:
    def __len__(self):
        raise ValueError


raised = False
try:
    operator.length_hint(RaisingLength(), 1.5)
except TypeError:
    raised = True
assert raised

assert operator.index(12) == 12
for value in (True, False, 0, -12, 10**30):
    result = operator.index(value)
    assert result == int(value)
    assert result.__class__ is int

for value in (2.0, "2", None, NeverEqual()):
    raised = False
    try:
        operator.index(value)
    except TypeError:
        raised = True
    assert raised


assert operator.__all__[0] == "abs"
assert operator.__all__[2] == "and_"
assert operator.__all__[19] == "imatmul"


class LeftMatmul:
    def __matmul__(self, other):
        return "left"


class RightMatmul:
    def __rmatmul__(self, other):
        return "right"


assert operator.matmul(LeftMatmul(), 1) == "left"
assert operator.__matmul__(LeftMatmul(), 1) == "left"
assert operator.matmul(1, RightMatmul()) == "right"
assert operator.imatmul(LeftMatmul(), 1) == "left"
assert operator.__imatmul__(LeftMatmul(), 1) == "left"
