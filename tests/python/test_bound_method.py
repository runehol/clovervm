from types import MethodType


class Receiver:
    def f(self, value, *, extra):
        return self, value, extra

    def __repr__(self):
        return "receiver"


o = Receiver()
m = MethodType(Receiver.f, o)
assert m.__class__ is MethodType
assert MethodType.__name__ == "method"
assert m.__func__ is Receiver.f
assert m.__self__ is o
assert callable(m)
assert m(1, extra=2) == (o, 1, 2)
assert m(*(3,), **{"extra": 4}) == (o, 3, 4)
assert repr(m) == "<bound method f of receiver>"

saved = o.f
assert saved.__func__ is Receiver.f
assert saved.__self__ is o
assert saved(5, extra=6) == (o, 5, 6)
assert getattr(o, "f").__self__ is o
assert Receiver.f.__get__(o, Receiver).__self__ is o
assert Receiver.f.__get__(o).__func__ is Receiver.f
assert Receiver.f.__get__(None, Receiver) is Receiver.f
getter = Receiver.f.__get__
assert getter(o).__self__ is o

for args in ((None,), (None, None)):
    try:
        getter(*args)
        assert False
    except TypeError as exc:
        assert str(exc) == "__get__(None, None) is invalid"

try:
    Receiver.f.__class__.__get__(o, o)
    assert False
except TypeError:
    pass


class Derived(Receiver):
    pass


def read_f(obj):
    return obj.f


def replacement(obj, value, *, extra):
    return obj, value + extra


class OtherDescriptor:
    def __get__(self, obj, owner):
        return 71


d = Derived()
assert Derived.f is Receiver.f
for i in range(3):
    assert read_f(d).__func__ is Receiver.f
d.f = replacement
assert read_f(d) is replacement
assert getattr(d, "f") is replacement
del d.f
assert read_f(d).__self__ is d
assert d.f(1, extra=2) == (d, 1, 2)
original = Receiver.f
Receiver.f = replacement
assert read_f(d).__func__ is replacement
assert saved.__func__ is original
assert saved(8, extra=9) == (o, 8, 9)
Receiver.f = OtherDescriptor()
assert read_f(d) == 71
Receiver.f = original


class IntrinsicOwner:
    convert = ord


assert ord.__class__ is Receiver.f.__class__
assert ord.__get__(None, IntrinsicOwner) is ord
assert ord.__get__("A")() == 65
bound_intrinsic = IntrinsicOwner().convert
assert bound_intrinsic.__func__ is ord
assert bound_intrinsic.__self__.__class__ is IntrinsicOwner


class MethodLookalike:
    def __init__(self):
        self.__func__ = Receiver.f
        self.__self__ = o


assert MethodType.__call__(m, 1, extra=2) == (o, 1, 2)
try:
    MethodType.__call__(MethodLookalike(), 1, extra=2)
    assert False
except TypeError as exc:
    assert str(exc) == "method.__call__ expects a method receiver"

for name in ("__func__", "__self__"):
    try:
        setattr(m, name, None)
        assert False
    except AttributeError:
        pass

try:
    MethodType(o, Receiver.f)
    assert False
except TypeError as exc:
    assert str(exc) == "first argument must be callable"

try:
    MethodType(Receiver.f, None)
    assert False
except TypeError as exc:
    assert str(exc) == "instance must not be None"

try:
    MethodType(callable=Receiver.f, receiver=o)
    assert False
except TypeError:
    pass


class Target:
    def __call__(self, receiver, value, *, extra):
        assert receiver.__class__ is Receiver
        return value + extra


def make_method():
    return MethodType(Target(), Receiver())


m = make_method()
assert repr(m) == "<bound method ? of receiver>"
assert m(5, extra=6) == 11
assert MethodType(m, 7)(extra=8) == 15


class CallSlot:
    __call__ = None


target = CallSlot()
assert MethodType(target, o).__func__ is target


class BadRepr:
    def __repr__(self):
        raise ValueError("receiver repr")


try:
    repr(MethodType(Receiver.f, BadRepr()))
    assert False
except ValueError as exc:
    assert str(exc) == "receiver repr"
