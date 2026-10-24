# Focused operator.call tests; also runnable against CPython.
import operator


def no_args():
    return 42


def named(a, b=2, *, scale=1):
    return (a + b) * scale


def capture(*args, **kwargs):
    return args, kwargs


assert operator.call(no_args) == 42
assert operator.call(named, 3) == 5
assert operator.call(named, 3, b=4, scale=2) == 14
assert operator.call(named, *(3,), **{"b": 4, "scale": 2}) == 14
assert operator.__call__ is operator.call
assert operator.__call__(no_args) == 42
assert "call" in operator.__all__

marker = []
captured = operator.call(capture, marker, obj=marker)
assert captured[0][0] is marker
assert captured[1]["obj"] is marker


class Receiver:
    def __init__(self, value):
        self.value = value

    def add(self, amount, *, scale=1):
        return (self.value + amount) * scale


receiver = operator.call(Receiver, value=5)
assert receiver.value == 5
# First-class bound methods are outside the VM's current callable support.
assert operator.call(Receiver.add, receiver, 2, scale=3) == 21


def identity(value):
    return value


assert operator.call(identity, marker) is marker

error = ValueError("target failed")


def fail():
    raise error


raised = False
try:
    operator.call(fail)
except ValueError as caught:
    assert caught is error
    raised = True
assert raised

raised = False
try:
    operator.call()
except TypeError:
    raised = True
assert raised

# The target is positional-only; a keyword named obj is forwarded only when
# the positional target is present.
raised = False
try:
    operator.call(obj=no_args)
except TypeError:
    raised = True
assert raised

raised = False
try:
    operator.call(42)
except TypeError:
    raised = True
assert raised
