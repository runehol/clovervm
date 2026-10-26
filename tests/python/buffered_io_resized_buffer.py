# CloverVM's callbacks do not hold a buffer export. Check the post-callback
# bounds explicitly rather than allowing a copy beyond the resized buffer.
import io
import _io

assert io.BufferedIOBase is _io.BufferedIOBase

class Stream(io.BufferedIOBase):
    def read(self, size):
        self.destination.clear()
        return b'A'
    def read1(self, size):
        self.destination[:] = b'x'
        return b'AB'
s = Stream()
for method in (io.BufferedIOBase.readinto, io.BufferedIOBase.readinto1):
    s.destination = bytearray(3)
    try:
        method(s, s.destination)
        assert False
    except ValueError:
        pass
s.close()

# Growing the destination does not enlarge the original read request.
class Growing(io.BufferedIOBase):
    def read(self, size):
        assert size == 1
        self.destination.extend(b'xx')
        return b'AB'
s = Growing()
s.destination = bytearray(1)
try:
    s.readinto(s.destination)
    assert False
except ValueError:
    pass
assert len(s.destination) == 3
s.close()
