import io

class Raw(io.RawIOBase):
    def __init__(self, parts):
        self.parts = parts
        self.calls = 0
    def readable(self):
        return True
    def readinto(self, b):
        self.calls += 1
        part = self.parts.pop(0)
        if part is None:
            return None
        b[:len(part)] = part
        return len(part)
for method in (io.BufferedReader.read, io.BufferedReader.readinto,
               io.BufferedReader.readinto1):
    raw = Raw([None])
    r = io.BufferedReader(raw, 4)
    if method is io.BufferedReader.read:
        assert method(r, 3) is None
    else:
        b = bytearray(b'...')
        assert method(r, b) is None and b == b'...'
    assert raw.calls == 1
    r.close()
for method in (io.BufferedReader.read1, io.BufferedReader.peek):
    raw = Raw([None])
    r = io.BufferedReader(raw, 4)
    assert method(r, 3) == b'' and raw.calls == 1
    r.close()
raw = Raw([b'ab', None, b'c', b''])
r = io.BufferedReader(raw, 4)
assert r.read(6) == b'ab'
assert r.read(3) == b'c'
r.close()
# readinto1 must stop after one short raw read rather than try to fill again.
raw = Raw([b'ab', b'c'])
r = io.BufferedReader(raw, 4)
b = bytearray(b'...')
assert r.readinto1(b) == 2 and b == b'ab.' and raw.calls == 1
r.close()
raw = Raw([b'ab', None])
r = io.BufferedReader(raw, 4)
assert r.peek() == b'ab'
assert r.read() == b'ab'
r.close()
