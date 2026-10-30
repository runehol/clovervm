import io
import os

class BufferedFirst(io.BufferedIOBase, io.RawIOBase):
    def __init__(self):
        self.marker = 42
    def read(self, size=-1):
        return b'B'
class RawFirst(io.RawIOBase, io.BufferedIOBase):
    def __init__(self):
        self.marker = 43
    def readinto(self, b):
        b[0] = 82
        return 1
for cls in (BufferedFirst, RawFirst):
    s = cls()
    assert isinstance(s, io.BufferedIOBase)
    assert isinstance(s, io.RawIOBase)
    assert s.marker >= 42 and not s.closed
    b = bytearray(b'..')
    expected = b'B.' if cls is BufferedFirst else b'R.'
    assert io.BufferedIOBase.readinto(s, b) == 1 and b == expected
    if cls is RawFirst:
        assert io.RawIOBase.read(s, 2) == b'R'
    else:
        try:
            io.RawIOBase.readinto(s, b)
            assert False
        except NotImplementedError:
            pass
    s.close()
    assert s.closed
class Parent(io.IOBase):
    pass
class Upgrade(Parent, io.BufferedIOBase):
    def __init__(self, value):
        self.value = value
s = Upgrade(9)
assert s.value == 9 and not s.closed
s.close()
# The larger FileIO prefix must win in either base ordering.
path = 'clover-buffered-base-' + str(os.getpid())
class FileFirst(io.FileIO, io.BufferedIOBase):
    pass
class BufferedFile(io.BufferedIOBase, io.FileIO):
    pass
for cls in (FileFirst, BufferedFile):
    with cls(path, 'w+') as f:
        assert f.fileno() >= 0 and not f.closed
        assert io.FileIO.write(f, b'AB') == 2
        f.seek(0)
        b = bytearray(b'....')
        assert io.FileIO.readinto(f, b) == 2 and b == b'AB..'
    assert f.closed
os.unlink(path)
