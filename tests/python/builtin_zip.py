# Zip behavior compared with CPython 3.14.5 Python/bltinmodule.c.


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


assert list(zip()) == []
assert list(zip(strict=True)) == []
assert list(zip([1, 2])) == [(1,), (2,)]
assert list(zip([1, 2], [3])) == [(1, 3)]
assert list(zip([], [3])) == []
assert list(zip([1], [2], [3])) == [(1, 2, 3)]
assert list(zip([1, 2], [3, 4], strict=True)) == [(1, 3), (2, 4)]
assert list(zip([], [], [], strict=True)) == []
assert list(zip([1, 2], [3], strict=0)) == [(1, 3)]
raises(TypeError, zip, iterables=[])
raises(TypeError, zip, [], True)
raises(TypeError, zip.__next__, object())
raises(TypeError, zip.__iter__, object())
raises(TypeError, zip.__new__, object, [])
raises(AttributeError, setattr, zip([]), "extra", 4)


class Sub(zip):
    pass


sub = Sub([5], [6], strict=True)
assert sub.__class__ is Sub and iter(sub) is sub
assert next(sub) == (5, 6)
sub.extra = 4
assert sub.extra == 4
