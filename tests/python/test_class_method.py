def function(cls, value, *, extra=0):
    return cls, value + extra


wrapper = classmethod(function)
assert wrapper.__func__ is function


class Base:
    f = wrapper


class Derived(Base):
    pass


obj = Derived()
from_class = Derived.f
from_instance = obj.f
assert Base.f.__self__ is Base
assert from_class.__self__ is Derived
assert from_instance.__self__ is Derived
assert from_class.__func__ is function
assert from_instance.__func__ is function
assert from_class(2) == (Derived, 2)
assert from_instance(2, extra=3) == (Derived, 5)
assert getattr(obj, "f").__self__ is Derived

assert wrapper.__get__(obj).__self__ is Derived
assert wrapper.__get__(obj, Base).__self__ is Base
assert wrapper.__get__(None, Derived).__self__ is Derived
assert wrapper.__get__(None, 42).__self__ == 42

for args in ((None,), (None, None)):
    getter = wrapper.__get__
    try:
        getter(*args)
        assert False
    except TypeError as error:
        assert str(error) == "__get__(None, None) is invalid"

try:
    classmethod.__get__(object(), obj)
    assert False
except TypeError:
    pass

obj.f = function
assert obj.f is function
del obj.f
assert obj.f.__self__ is Derived
obj.local = wrapper
assert obj.local is wrapper

bad = classmethod(42).__get__(None, Derived)
assert bad.__func__ == 42
assert bad.__self__ is Derived
try:
    bad()
    assert False
except TypeError:
    pass

try:
    wrapper.__func__ = None
    assert False
except AttributeError:
    pass

original_getter = classmethod.__get__
try:
    classmethod.__get__ = function
    assert False
except BaseException as error:
    assert isinstance(error, (AttributeError, TypeError))
assert classmethod.__get__ is original_getter
