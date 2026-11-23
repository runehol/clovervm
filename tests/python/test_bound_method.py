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
assert o.f is Receiver.f


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
