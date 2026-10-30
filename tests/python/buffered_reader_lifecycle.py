import io

class Failure(Exception):
    pass
error = Failure('flush')
class Raw(io.RawIOBase):
    def __init__(self):
        self.closes = 0
        self.fail = False
    def readable(self):
        return True
    def readinto(self, b):
        b[:3] = b'abc'
        return 3
    def flush(self):
        if self.fail:
            raise error
    def close(self):
        self.closes += 1
        self.fail = False
        io.RawIOBase.close(self)
raw = Raw()
r = io.BufferedReader(raw, 4)
raw.fail = True
try:
    r.close()
    assert False
except Failure as caught:
    assert caught is error
assert raw.closes == 1 and raw.closed and r.closed
r.close()
assert raw.closes == 1
# close dynamically invokes a reader subclass's flush override.
class Reader(io.BufferedReader):
    def flush(self):
        self.flushed = True
        io.BufferedReader.flush(self)
raw = Raw()
r = Reader(raw, 4)
r.close()
assert r.flushed and raw.closed

# Successful explicit reinitialization resets the reader's own base-close flag.
r.__init__(Raw(), 4)
assert not r.closed and r.read(1) == b'a'
r.close()
# A failed detach keeps both the raw attachment and unread buffered data.
raw = Raw()
r = io.BufferedReader(raw, 4)
assert r.read(1) == b'a'
raw.fail = True
try:
    r.detach()
    assert False
except Failure as caught:
    assert caught is error
assert r.raw is raw and not r.closed
raw.fail = False
assert r.read(2) == b'bc'
assert r.detach() is raw
raw.close()
