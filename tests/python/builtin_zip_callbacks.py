# Zip behavior compared with CPython 3.14.5 Python/bltinmodule.c.


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


log = []


class Source:
    def __init__(self, name, values):
        self.name = name
        self.values = iter(values)

    def __iter__(self):
        log.append("iter " + self.name)
        return self

    def __next__(self):
        log.append("next " + self.name)
        value = next(self.values)
        if value == "stop":
            raise StopIteration("payload")
        if value == "error":
            raise ValueError("source failure")
        return value


class Truth:
    def __bool__(self):
        log.append("bool")
        return True


marker = []
z = zip([marker, marker], [marker, marker])
a = next(z)
b = next(z)
assert a.__class__ is tuple and b.__class__ is tuple
assert a is not b and a[0] is marker and b[1] is marker
assert a == (marker, marker) and iter(z) is z

log.clear()


class Reentrant:
    def __init__(self):
        self.position = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.position += 1
        if self.position == 1:
            log.append(next(z))
            return "outer"
        if self.position == 2:
            return "inner"
        raise StopIteration


z = zip(Reentrant(), [10, 20], strict=True)
assert next(z) == ("outer", 20)
assert log == [("inner", 10)]
raises(StopIteration, next, z)

# An exception from strict's truth callback precedes all iterator acquisition.
class BadTruth:
    def __bool__(self):
        raise ValueError("truth failure")


log.clear()
assert raises(ValueError, zip, Source("a", []), strict=BadTruth()).args == ("truth failure",)
assert log == []

# Iterator acquisition is eager, left to right, and stops at the first error.
raises(TypeError, zip, Source("a", []), 1, Source("b", []))
assert log == ["iter a"]

# Repeated references share a single source, consumed left to right.
shared = iter([1, 2, 3, 4])
assert list(zip(shared, shared, strict=True)) == [(1, 2), (3, 4)]

# Trusted methods keep their dependencies when public builtins are rebound.
import builtins

Enum = enumerate
Zip = zip
Next = next
Iter = iter
Len = len
Tuple = tuple
try:
    builtins.enumerate = None
    builtins.zip = None
    builtins.next = None
    builtins.iter = None
    builtins.len = None
    builtins.tuple = None
    e = Enum([4, 5], 8)
    z = Zip([1, 2], [3, 4], strict=True)
    assert Iter(e) is e and Iter(z) is z
    assert Next(e) == (8, 4) and Next(e) == (9, 5)
    assert Next(z) == (1, 3) and Next(z) == (2, 4)
finally:
    builtins.enumerate = Enum
    builtins.zip = Zip
    builtins.next = Next
    builtins.iter = Iter
    builtins.len = Len
    builtins.tuple = Tuple

# Each callback creates an element that only the in-progress row can retain.
# The C++ companion reruns this file with reclamation at every safepoint.
class Fresh:
    def __init__(self):
        self.position = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.position += 1
        if self.position > 2:
            raise StopIteration
        return [self.position]


z = zip(Fresh(), Fresh(), strict=True)
a = next(z)
b = next(z)
assert a == ([1], [1]) and b == ([2], [2])
assert a[0] is not a[1] and a[0] is not b[0]
e = enumerate(Fresh(), 2**100)
a = next(e)
b = next(e)
assert a == (2**100, [1]) and b == (2**100 + 1, [2])
assert a[1] is not b[1]
