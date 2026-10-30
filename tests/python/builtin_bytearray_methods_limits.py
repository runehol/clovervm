# Intentional, agreed CloverVM limit; not a CPython differential test.
# General __index__ objects are rejected without callbacks or alias mutation.
calls = []
a = bytearray(b'abc')
class Index:
    def __index__(self):
        calls.append(1)
        a.clear()
        return 0
    def __eq__(self, other):
        calls.append(2)
        a.clear()
        return True
x = Index()
for operation in range(4):
    try:
        if operation == 0:
            a.pop(x)
        elif operation == 1:
            a.insert(x, 65)
        elif operation == 2:
            a.insert(0, x)
        else:
            a.remove(x)
    except TypeError:
        pass
    else:
        raise AssertionError
    assert calls == [] and a == b'abc'
