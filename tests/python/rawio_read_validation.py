import io

class Index:
    def __init__(self, value):
        self.value = value
        self.calls = 0
    def __index__(self):
        self.calls += 1
        return self.value
class R(io.RawIOBase):
    def readinto(self, b):
        b[:] = b'XY'
        return self.count
r = R()
for value in (0, 1, 2, False, True):
    r.count = value
    assert r.read(2) == b'XY'[:value]
    index = Index(value)
    r.count = index
    size = Index(2)
    assert r.read(size) == b'XY'[:value]
    assert index.calls == 1 and size.calls == 1
for value in (-1, 3, 2**100, -(2**100)):
    r.count = value
    try:
        r.read(2)
        assert False
    except ValueError:
        pass
    r.count = Index(value)
    try:
        r.read(2)
        assert False
    except ValueError:
        pass
for value in (1.5, '1', [], Index(None), Index(1.5), Index(Index(1))):
    r.count = value
    try:
        r.read(2)
        assert False
    except TypeError:
        pass
r.count = None
assert r.read(2) is None
r.close()

class IntTrap(int):
    def __index__(self):
        raise AssertionError('stored int payload must win')
    def __lt__(self, other):
        raise AssertionError('stored int payload must win')
    def __gt__(self, other):
        raise AssertionError('stored int payload must win')
    def __int__(self):
        raise AssertionError('stored int payload must win')
r = R()
r.count = IntTrap(1)
assert r.read(IntTrap(2)) == b'X'
r.close()

class Missing:
    pass
bad = Missing()
bad.__index__ = 1
try:
    R().read(bad)
    assert False
except TypeError:
    pass
