# Original regressions for augmented arithmetic, checked against CPython 3.14.5.
# No builtin in-place methods are added by this core dispatch slice.

def do_add(a, b):
    a += b
    return a

def immediate_add(a):
    a += 3
    return a

def do_sub(a, b):
    a -= b
    return a

def immediate_sub(a):
    a -= 3
    return a

def do_mul(a, b):
    a *= b
    return a

def immediate_mul(a):
    a *= 3
    return a

def do_matmul(a, b):
    a @= b
    return a

def immediate_matmul(a):
    a @= 3
    return a

def do_truediv(a, b):
    a /= b
    return a

def immediate_truediv(a):
    a /= 3
    return a

def do_floordiv(a, b):
    a //= b
    return a

def immediate_floordiv(a):
    a //= 3
    return a

def do_pow(a, b):
    a **= b
    return a

def immediate_pow(a):
    a **= 3
    return a

def do_lshift(a, b):
    a <<= b
    return a

def immediate_lshift(a):
    a <<= 3
    return a

def do_rshift(a, b):
    a >>= b
    return a

def immediate_rshift(a):
    a >>= 3
    return a

def do_mod(a, b):
    a %= b
    return a

def immediate_mod(a):
    a %= 3
    return a

def do_or(a, b):
    a |= b
    return a

def immediate_or(a):
    a |= 3
    return a

def do_and(a, b):
    a &= b
    return a

def immediate_and(a):
    a &= 3
    return a

def do_xor(a, b):
    a ^= b
    return a

def immediate_xor(a):
    a ^= 3
    return a

class Replacement:
    def __iadd__(self, other):
        return ("add", other)

# In-place dispatch precedes even a strict subclass's reflected method.
events = []
class Left:
    def __iadd__(self, other):
        events.append("iadd")
        return NotImplemented
    def __add__(self, other):
        events.append("add")
        return "left"
class Right(Left):
    def __radd__(self, other):
        events.append("radd")
        return "right"
assert do_add(Left(), Right()) == "right"
assert events == ["iadd", "radd"]
events.clear()
class Refuses(Right):
    def __radd__(self, other):
        events.append("radd")
        return NotImplemented
assert do_add(Left(), Refuses()) == "left"
assert events == ["iadd", "radd", "add"]

# A declined in-place method must be called again on every cache replay.
class Sometimes:
    def __init__(self):
        self.calls = 0
    def __iadd__(self, other):
        self.calls += 1
        if self.calls < 3:
            return NotImplemented
        return "inplace"
    def __add__(self, other):
        return "fallback"
a = Sometimes()
assert do_add(a, 1) == "fallback"
assert do_add(a, 1) == "fallback"
assert do_add(a, 1) == "inplace"
assert a.calls == 3

# Cache guards must cover earlier negative lookups and inherited methods.
class Mutable:
    def __add__(self, other):
        return "add"
def added(self, other):
    return "added"
def changed(self, other):
    return "changed"
a = Mutable()
assert do_add(a, 1) == "add"
assert do_add(a, 1) == "add"
Mutable.__iadd__ = added
assert do_add(a, 1) == "added"
assert do_add(a, 1) == "added"
Mutable.__iadd__ = changed
assert do_add(a, 1) == "changed"
del Mutable.__iadd__
assert do_add(a, 1) == "add"
class Child(Mutable):
    pass
b = Child()
assert immediate_add(b) == "add"
Mutable.__iadd__ = added
assert immediate_add(b) == "added"
Mutable.__iadd__ = changed
assert immediate_add(b) == "changed"
del Mutable.__iadd__
assert immediate_add(b) == "add"

# None is a successful result; instance attributes do not provide special methods.
class NoneResult:
    def __iadd__(self, other):
        return None
assert do_add(NoneResult(), 1) is None
a = Mutable()
a.__iadd__ = added
assert do_add(a, 1) == "add"

failure = ValueError("stop")
class Raises:
    def __iadd__(self, other):
        raise failure
    def __add__(self, other):
        raise AssertionError("fallback after exception")
class Box:
    pass
box = Box()
box.value = Raises()
original = box.value
try:
    box.value += 3
    assert False
except ValueError as error:
    assert error is failure
assert box.value is original

# Receiver/key/load/RHS/op/store ordering, with independent result writeback.
events.clear()
class NewValue:
    def __iadd__(self, other):
        events.append("op")
        return other
class Container:
    def __init__(self):
        self.value = NewValue()
    def __getitem__(self, key):
        events.append("get")
        return self.value
    def __setitem__(self, key, value):
        events.append("set")
        self.value = value
container = Container()
def receiver():
    events.append("receiver")
    return container
def key():
    events.append("key")
    return 0
def rhs():
    events.append("rhs")
    return ["replacement"]
receiver()[key()] += rhs()
assert events == ["receiver", "key", "get", "rhs", "op", "set"]
assert container.value == ["replacement"]
container.value = NewValue()
events.clear()
receiver().value += rhs()
assert events == ["receiver", "rhs", "op"]
assert container.value == ["replacement"]

container.value = Raises()
original = container.value
events.clear()
try:
    receiver()[key()] += rhs()
    assert False
except ValueError as error:
    assert error is failure
assert events == ["receiver", "key", "get", "rhs"]
assert container.value is original

# Existing SMI overflow paths must retain operands for ordinary fallback.
assert immediate_add(2**62 - 1) == 2**62 + 2
assert do_sub(-(2**62), 1) == -(2**62) - 1
assert do_mul(2**40, 2**40) == 2**80
assert immediate_add(1.5) == 4.5
assert do_truediv(3, 2) == 1.5
for operation in (do_truediv, do_floordiv, do_mod):
    try:
        operation(1, 0)
        assert False
    except ZeroDivisionError:
        pass
try:
    do_lshift(1, -1)
    assert False
except ValueError:
    pass

# Recompute fallback lookup after a Python in-place candidate mutates its class.
class ChangesFallback:
    def __iadd__(self, other):
        ChangesFallback.__add__ = changed
        return NotImplemented
    def __add__(self, other):
        return "old"
assert do_add(ChangesFallback(), 1) == "changed"

# The immediate subscript path must store the independent result too.
container.value = Replacement()
container[0] += 3
assert container.value == ("add", 3)
box.value = Replacement()
box.value += 3
assert box.value == ("add", 3)

class NoOperators:
    pass
try:
    do_add(NoOperators(), 1)
    assert False
except TypeError as error:
    assert "+=" in str(error)
assert do_add(True, 3) == 4

# Tiering occurs on function entry, with a budget of 100 calls. Warm both
# numeric forms before passing Python objects, so JIT guard exits must resume
# augmented dispatch rather than ordinary addition.
def hot_add(total, value):
    total += value
    return total
def hot_add_three(total):
    total += 3
    return total
for warm in range(110):
    assert hot_add(19, 3) == 22
    assert hot_add_three(19) == 22
class Counter:
    def __init__(self):
        self.value = 0
    def __iadd__(self, other):
        self.value += other
        return self
    def __add__(self, other):
        raise AssertionError("ordinary add used after JIT guard exit")
counter = Counter()
assert hot_add(counter, 3) is counter
assert counter.value == 3
assert hot_add_three(counter) is counter
assert counter.value == 6

class SameTypeRefuses:
    def __iadd__(self, other):
        return NotImplemented
    def __add__(self, other):
        return NotImplemented
    def __radd__(self, other):
        raise AssertionError("same-type reflected method must not run")
try:
    do_add(SameTypeRefuses(), SameTypeRefuses())
    assert False
except TypeError:
    pass

class MutatesRight:
    def __iadd__(self, other):
        NewlyReflected.__radd__ = changed
        return NotImplemented
    def __add__(self, other):
        return "left"
class NewlyReflected(MutatesRight):
    pass
assert do_add(MutatesRight(), NewlyReflected()) == "changed"
