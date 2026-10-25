# CloverVM managed-count rooting regression, not adapted from CPython tests.
#
# Run this in CloverVM or with the production stdlib/itertools.py loaded into
# a host harness. A broader local native CPython 3.14.5 probe passes its
# assertions but then segfaults during GC after this operation. Keep this separate
# from the portable count reference tests: do not treat that crash as the
# desired behavior. Both active next() calls must retain their current value.

from itertools import count


target = None
calls = 0
seen = []


class ReentrantInteger(int):
    def __add__(self, other):
        global calls
        calls += 1
        if calls == 1:
            seen.append(next(target))
        return 7


def exercise():
    global target
    first = ReentrantInteger(2)
    target = count(first, 0)
    assert next(target) is first
    assert calls == 2
    assert len(seen) == 1
    assert seen[0] is first
    assert next(target) == 7


exercise()
assert seen[0] == 2


class ParameterOnlyInteger(int):
    def __add__(self, other):
        global calls
        calls += 1
        if calls == 1:
            next(target)
        return 7


# No external owner of the initial value: after the callback returns, the
# outer next() frame must still root it even though the counter now stores 7.
calls = 0
target = count(ParameterOnlyInteger(2), 0)
result = next(target)
assert result.__class__ is ParameterOnlyInteger
assert int.__index__(result) == 2
assert calls == 2
