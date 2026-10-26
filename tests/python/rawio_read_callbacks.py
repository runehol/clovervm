import io
import builtins

class Index:
    def __index__(self):
        self.buffer[:] = b'changed'
        return 3
class R(io.RawIOBase):
    def readinto(self, buffer):
        self.buffer = buffer
        index = Index()
        index.buffer = buffer
        return index
r = R()
assert r.read(4) == b'cha'
assert r.buffer == b'changed'
r.buffer[:] = b'original'
copy = r.read(4)
r.buffer[0] = 88
assert copy == b'cha' and r.buffer == b'Xhanged'
r.close()

class Failure(Exception):
    pass
error = Failure('callback')
class BadIndex:
    def __index__(self):
        raise error
class ReturnsIndex(io.RawIOBase):
    def readinto(self, buffer):
        return BadIndex()
r = ReturnsIndex()
try:
    r.read(2)
    assert False
except Failure as caught:
    assert caught is error
try:
    r.read(BadIndex())
    assert False
except Failure as caught:
    assert caught is error
r.close()

# Normal callback exceptions retain identity too.
class Raising(io.RawIOBase):
    def readinto(self, buffer):
        raise error
r = Raising()
try:
    r.read(2)
    assert False
except Failure as caught:
    assert caught is error
r.close()

# Private bootstrap bindings must not follow rebinding of public builtins.
class One(io.RawIOBase):
    def readinto(self, buffer):
        buffer[0] = 65
        return 1
r = One()
saved_bytes = builtins.bytes
saved_bytearray = builtins.bytearray
builtins.bytes = None
builtins.bytearray = None
try:
    assert r.read(3) == b'A'
finally:
    builtins.bytes = saved_bytes
    builtins.bytearray = saved_bytearray
r.close()
