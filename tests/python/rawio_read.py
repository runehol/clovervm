import io

class Reader(io.RawIOBase):
    def __init__(self):
        self.requests = []
        self.parts = [b'ab', b'c', None, b'']
    def readinto(self, buffer):
        self.requests.append(len(buffer))
        part = self.parts.pop(0)
        if part is None:
            return None
        buffer[:len(part)] = part
        return len(part)
r = Reader()
assert r.read(5) == b'ab'
assert r.read(2) == b'c'
assert r.read(3) is None
assert r.read(4) == b''
assert r.requests == [5, 2, 3, 4]
r.close()

class Zero(io.RawIOBase):
    def readinto(self, buffer):
        self.buffer = buffer
        return 0
r = Zero()
assert r.read(False) == b'' and r.buffer == b''
assert r.read(True) == b'' and len(r.buffer) == 1
r.close()

class All(io.RawIOBase):
    def readall(self):
        self.calls += 1
        return self.marker
r = All()
r.calls = 0
r.marker = [42]
assert r.read() is r.marker
assert r.read(-1) is r.marker
assert r.read(-2) is r.marker
assert r.read(-(2**60)) is r.marker
assert r.calls == 4
for size in (None, '1', 1.0):
    try:
        r.read(size)
        assert False
    except TypeError:
        pass
for size in (2**100, -(2**100)):
    try:
        r.read(size)
        assert False
    except OverflowError:
        pass
assert r.calls == 4
r.close()

class SizeTrap:
    def __index__(self):
        raise AssertionError('receiver must be validated before conversion')
try:
    io.RawIOBase.read(object(), SizeTrap())
    assert False
except TypeError:
    pass
try:
    io.RawIOBase.readall(object())
    assert False
except TypeError:
    pass
for method in (io.RawIOBase.read, io.RawIOBase.readall):
    try:
        method(io.IOBase())
        assert False
    except TypeError:
        pass
try:
    io.RawIOBase().read(size=1)
    assert False
except TypeError:
    pass
