# Portions adapted from CPython Lib/test/test_enumerate.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


assert list(enumerate([])) == []
assert list(enumerate("ab")) == [(0, "a"), (1, "b")]
assert list(enumerate((5, 6), -2)) == [(-2, 5), (-1, 6)]
assert list(enumerate(iterable=[5], start=7)) == [(7, 5)]
assert list(enumerate(start=7, iterable=[5])) == [(7, 5)]
assert list(enumerate([5], start=True)) == [(1, 5)]
assert next(enumerate([5], True))[0].__class__ is int
for start in (-(2**100), -(2**58) - 1, 2**58 - 1, 2**62 - 1, 2**63 - 1, 2**100):
    assert list(enumerate([4, 5], start)) == [(start, 4), (start + 1, 5)]
raises(TypeError, enumerate)
raises(TypeError, enumerate, [], 0, 1)
raises(TypeError, enumerate, [], 1.5)
raises(TypeError, enumerate, [], iterable=[])
raises(TypeError, enumerate, [], invalid=1)
raises(TypeError, enumerate, start=0)
raises(TypeError, enumerate.__next__, object())
raises(TypeError, enumerate.__iter__, object())
raises(TypeError, enumerate.__new__, object, [])


class Sub(enumerate):
    pass


sub = Sub([9], 10)
assert sub.__class__ is Sub
assert iter(sub) is sub
assert next(sub) == (10, 9)
sub.extra = 4
assert sub.extra == 4
raises(AttributeError, setattr, enumerate([]), "extra", 4)


class IntSub(int):
    def __index__(self):
        raise AssertionError("must use builtin integer payload")

    def __add__(self, other):
        raise AssertionError("counter must be an exact integer")


index = next(enumerate([4], IntSub(2**100)))[0]
assert index == 2**100
assert index.__class__ is int

log = []


class Source:
    def __init__(self):
        self.position = 0

    def __iter__(self):
        log.append("iter")
        return self

    def __next__(self):
        log.append("next")
        self.position += 1
        if self.position == 1:
            raise StopIteration("payload")
        if self.position == 3:
            raise ValueError("source failure")
        if self.position > 4:
            raise StopIteration
        return self.position


source = Source()
e = enumerate(source, 8)
assert log == ["iter"]
assert raises(StopIteration, next, e).args == ("payload",)
assert next(e) == (8, 2)
assert raises(ValueError, next, e).args == ("source failure",)
assert next(e) == (9, 4)
raises(StopIteration, next, e)
raises(StopIteration, next, e)
assert source.position == 6

log.clear()
raises(TypeError, enumerate, Source(), 0.5)
assert log == []


class Reentrant:
    def __init__(self):
        self.position = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.position += 1
        if self.position == 1:
            log.append(next(e))
            return "outer"
        if self.position == 2:
            return "inner"
        raise StopIteration


e = enumerate(Reentrant(), 2**100)
assert next(e) == (2**100 + 1, "outer")
assert log == [(2**100, "inner")]

# Held output tuples remain distinct and unchanged; elements retain identity.
marker = []
e = enumerate([marker, marker])
a = next(e)
b = next(e)
assert a.__class__ is tuple and b.__class__ is tuple
assert a is not b and a == (0, marker) and b == (1, marker)
assert a[1] is marker and b[1] is marker
assert iter(e) is e
