import io

assert issubclass(io.BufferedReader, io.BufferedIOBase)
assert io.DEFAULT_BUFFER_SIZE == 128 * 1024
class Raw(io.RawIOBase):
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.calls = []
    def readable(self):
        return True
    def readinto(self, b):
        self.calls.append(len(b))
        part = self.data[self.pos:self.pos + len(b)]
        b[:len(part)] = part
        self.pos += len(part)
        return len(part)
raw = Raw(b'abcdefghijk')
r = io.BufferedReader(raw, 4)
assert r.read(1) == b'a'
assert r.read(1) == b'b'
assert r.read(1) == b'c'
assert r.read(1) == b'd'
assert raw.calls == [4]
assert r.peek() == b'efgh'
assert raw.calls == [4, 4]
assert r.read1(1) == b'e'
assert r.peek(100) == b'fgh'
assert r.read(5) == b'fghij'
assert r.read() == b'k'
assert r.read(2) == b''
assert r.read1() == b''
assert r.peek() == b''
assert r.readinto(bytearray()) == 0
r.close()
assert r.closed and raw.closed
r.close()
# read1 uses at most one raw read and does not prefetch beyond its request.
raw = Raw(b'abcdef')
r = io.BufferedReader(raw, 4)
assert r.read1(2) == b'ab'
assert raw.calls == [2]
assert r.read1(0) == b'' and raw.calls == [2]
assert r.read1(10) == b'cdef' and raw.calls == [2, 10]
r.close()
# readinto1 may combine buffered bytes with one large raw transfer.
raw = Raw(b'abcdefghijklmno')
r = io.BufferedReader(raw, 4)
assert r.read(2) == b'ab'
b = bytearray(b'........')
assert r.readinto1(b) == 8 and b == b'cdefghij'
assert raw.calls == [4, 6]
b = bytearray(b'........')
assert r.readinto(b) == 5 and b == b'klmno...'
r.close()
# A short raw read is not EOF.
class Short(Raw):
    def readinto(self, b):
        self.calls.append(len(b))
        part = self.data[self.pos:self.pos + 1]
        b[:len(part)] = part
        self.pos += len(part)
        return len(part)
raw = Short(b'abc')
r = io.BufferedReader(raw, 4)
assert r.read(3) == b'abc'
assert len(raw.calls) == 3
r.close()
class Reader(io.BufferedReader):
    pass
raw = Raw(b'XYZ')
r = Reader(raw, 2)
r.extra = 42
assert r.read() == b'XYZ' and r.extra == 42
r.close()
# Failed construction leaves the supplied raw stream open.
raw = Raw(b'abc')
for size in (0, -1):
    try:
        io.BufferedReader(raw, size)
        assert False
    except ValueError:
        pass
assert not raw.closed
raw.close()
