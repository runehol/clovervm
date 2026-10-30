import io

class Shrink(io.RawIOBase):
    def readinto(self, buffer):
        buffer.clear()
        return 1
r = Shrink()
try:
    r.read(3)
    assert False
except ValueError:
    pass
r.close()

class ShrinkIndex:
    def __index__(self):
        self.buffer.clear()
        return 1
class Reader(io.RawIOBase):
    def readinto(self, buffer):
        index = ShrinkIndex()
        index.buffer = buffer
        return index
r = Reader()
try:
    r.read(3)
    assert False
except ValueError:
    pass
r.close()

class Grow(io.RawIOBase):
    def readinto(self, buffer):
        buffer.extend(b'ABCD')
        return 4
r = Grow()
try:
    r.read(2)
    assert False
except ValueError:
    pass
r.close()
