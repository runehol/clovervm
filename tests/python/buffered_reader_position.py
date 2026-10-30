import io
import os

path = 'clover-reader-' + str(os.getpid())
with io.FileIO(path, 'w+') as f:
    assert f.write(b'abcdefghijkl') == 12
    f.seek(0)
    r = io.BufferedReader(f, 4)
    assert r.fileno() == f.fileno()
    assert r.readable() and r.seekable() and not r.writable()
    assert r.read(1) == b'a'
    assert f.tell() == 4 and r.tell() == 1
    assert r.seek(1, 1) == 2
    assert r.read(2) == b'cd'
    assert r.tell() == 4
    assert r.seek(-2, 2) == 10
    assert r.read(None) == b'kl'
    assert r.seek(0) == 0
    assert r.peek() == b'abcd'
    raw = r.detach()
    assert raw is f and raw.tell() == 4 and not raw.closed
    assert r.raw is None
    try:
        r.read(1)
        assert False
    except ValueError:
        pass
os.unlink(path)
# A failed seek preserves unread data.
class Raw(io.RawIOBase):
    def readable(self):
        return True
    def readinto(self, b):
        b[:3] = b'abc'
        return 3
    def seek(self, offset, whence=0):
        raise OSError('seek failed')
r = io.BufferedReader(Raw(), 4)
assert r.read(1) == b'a'
try:
    r.seek(0)
    assert False
except OSError:
    pass
assert r.read(2) == b'bc'
r.close()

class Seeks(io.RawIOBase):
    def __init__(self):
        self.calls = 0
    def readable(self):
        return True
    def seekable(self):
        return True
    def seek(self, offset, whence=0):
        self.calls += 1
        return 0
raw = Seeks()
r = io.BufferedReader(raw, 4)
raw.calls = 0
for whence in (99, -1):
    try:
        r.seek(0, whence)
        assert False
    except ValueError:
        pass
for whence in (2**31, -(2**31)-1):
    try:
        r.seek(0, whence)
        assert False
    except OverflowError:
        pass
assert raw.calls == 0
r.close()
