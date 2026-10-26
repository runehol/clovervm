import io

class Raw(io.RawIOBase):
    def __init__(self):
        self.data = bytearray()
        self.calls = []
        self.limit = 100
    def writable(self):
        return True
    def write(self, data):
        data = bytes(data)
        self.calls.append(data)
        count = min(len(data), self.limit)
        self.data.extend(data[:count])
        return count
    def tell(self):
        return len(self.data)
raw = Raw()
w = io.BufferedWriter(raw, 4)
assert issubclass(io.BufferedWriter, io.BufferedIOBase)
assert not w.readable() and w.writable() and not w.closed
assert w.write(b'') == 0 and raw.calls == []
assert w.write(b'ab') == 2
assert w.write(bytearray(b'cd')) == 2
assert raw.calls == [] and w.tell() == 4
raw.limit = 2
assert w.flush() is None
assert raw.calls == [b'abcd', b'cd'] and raw.data == b'abcd'
assert w.tell() == 4
w.close()
assert w.closed and raw.closed
w.close()
raw = Raw()
raw.limit = 3
w = io.BufferedWriter(raw, 4)
assert w.write(b'abcdefghij') == 10
assert w.tell() == 10
w.flush()
assert raw.data == b'abcdefghij'
w.close()
# Mutable input is snapshotted before returning to the caller.
raw = Raw()
w = io.BufferedWriter(raw, 4)
b = bytearray(b'AB')
assert w.write(b) == 2
b[:] = b'ZZ'
w.flush()
assert raw.data == b'AB'
w.close()
class Writer(io.BufferedWriter):
    pass
raw = Raw()
w = Writer(raw, 4)
w.extra = 42
with w as entered:
    assert entered is w
    assert w.write(b'xy') == 2
assert raw.data == b'xy' and w.extra == 42 and w.closed
raw = Raw()
for size in (0, -1):
    try:
        io.BufferedWriter(raw, size)
        assert False
    except ValueError:
        pass
assert not raw.closed
raw.close()
raw = Raw()
w = io.BufferedWriter(raw=raw, buffer_size=4)
assert w.write(b'ab') == 2
try:
    w.write(data=b'x')
    assert False
except TypeError:
    pass
w.close()
assert raw.data == b'ab'
