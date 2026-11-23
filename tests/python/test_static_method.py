def function(value, *, extra=0):
    return value + extra


wrapper = staticmethod(function)
assert wrapper.__func__ is function
assert wrapper.__get__(object()) is function
assert wrapper.__get__(None, object) is function

try:
    wrapper.__get__(None)
    assert False
except TypeError as error:
    assert str(error) == "__get__(None, None) is invalid"

try:
    staticmethod.__get__(object(), object())
    assert False
except TypeError:
    pass

try:
    wrapper.__func__ = None
    assert False
except AttributeError:
    pass

original_getter = staticmethod.__get__
try:
    staticmethod.__get__ = function
    assert False
except BaseException as error:
    assert isinstance(error, (AttributeError, TypeError))
assert staticmethod.__get__ is original_getter


class C:
    def f(value, *, extra=0):
        return value + extra

    f = staticmethod(f)
    number = staticmethod(42)
    nothing = staticmethod(None)


class Child(C):
    pass


o = Child()
assert o.f is C.f
assert Child.f is C.f
assert getattr(o, "f") is C.f
from_class = C.f
from_instance = o.f
assert from_class(2) == 2
assert from_instance(2, extra=3) == 5
assert from_instance(*(4,), **{"extra": 5}) == 9
assert C.number == o.number == 42
assert C.nothing is None and o.nothing is None
o.f = function
assert o.f is function
del o.f
assert o.f is C.f
o.local = wrapper
assert o.local is wrapper
