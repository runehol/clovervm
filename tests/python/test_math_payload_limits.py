# Intentional VM limits: builtin payload conversion ignores custom hooks.
# These assertions are not CPython differential expectations.
import math


class HookInt(int):
    def __float__(self):
        raise AssertionError


class FloatLike:
    def __float__(self):
        raise AssertionError


class IndexLike:
    def __index__(self):
        raise AssertionError


assert not math.isnan(HookInt(0))
assert not math.isnan(HookInt(-1))
try:
    math.isnan(HookInt(-(10**400)))
    assert False
except OverflowError:
    pass
for function in (math.log, math.isnan):
    for value in (FloatLike(), IndexLike()):
        try:
            function(value)
            assert False
        except TypeError:
            pass

# The pre-existing wrapper call conventions are outside this repair.
assert math.log(x=1, base=None) == 0.0
assert not math.isnan(x=1)
