# Intentional, agreed CloverVM limits; not a CPython differential test.
a = bytearray.__new__(bytearray, b"ab")
assert a == b"ab"
try:
    a.__init__(b"c")
except AttributeError:
    pass
else:
    raise AssertionError
try:
    class Child(bytearray):
        pass
except TypeError:
    pass
else:
    raise AssertionError
try:
    bytearray.__new__(object)
except TypeError:
    pass
else:
    raise AssertionError

calls = []
class Index:
    def __index__(self):
        calls.append(1)
        return 1
x = Index()
for operation in range(5):
    try:
        if operation == 0:
            bytearray(x)
        elif operation == 1:
            a[x]
        elif operation == 2:
            a[x] = 1
        elif operation == 3:
            a.append(x)
        else:
            a[x:] = b"z"
    except TypeError:
        pass
    else:
        raise AssertionError
assert calls == [] and a == b"ab"
for operation in range(5):
    try:
        if operation == 0:
            bytearray(b"a", "ascii")
        elif operation == 1:
            bytearray("a", encoding="ascii")
        elif operation == 2:
            bytearray(unknown=1)
        elif operation == 3:
            a.extend(source=b"a")
        else:
            bytearray.__len__(object())
    except TypeError:
        pass
    else:
        raise AssertionError
