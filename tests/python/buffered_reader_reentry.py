import io

class Raw(io.RawIOBase):
    def readable(self):
        return True
    def readinto(self, b):
        if self.action == 0:
            self.reader.read(1)
        elif self.action == 1:
            self.reader.seek(0)
        elif self.action == 2:
            self.reader.detach()
        elif self.action == 3:
            self.reader.close()
        elif self.action == 4:
            self.reader.__init__(self, 4)
        b[0] = 65
        return 1
for action in (0, 1, 2, 3, 4):
    raw = Raw()
    r = io.BufferedReader(raw, 4)
    raw.reader = r
    raw.action = action
    try:
        r.read(1)
        assert False
    except RuntimeError:
        pass
    raw.action = 5
    assert r.read(1) == b'A'
    r.close()
# Safety guard for callback-resized scratch storage (no exported view is held).
class Shrink(io.RawIOBase):
    def readable(self):
        return True
    def readinto(self, b):
        if self.shrink:
            b.clear()
            return 1
        b[:2] = b'OK'
        return 2
raw = Shrink()
raw.shrink = True
r = io.BufferedReader(raw, 4)
try:
    r.read(1)
    assert False
except ValueError:
    pass
raw.shrink = False
assert r.read(2) == b'OK'
r.close()
# Property incompatibilities accepted for this slice.
class Empty(io.RawIOBase):
    def readable(self):
        return True
    def readinto(self, b):
        return 0
raw = Empty()
r = io.BufferedReader(raw, 4)
raw.close()
assert not r.closed
try:
    r.read(1)
    assert False
except ValueError:
    pass
assert not hasattr(r, 'name') and not hasattr(r, 'mode')
r.close()
assert r.closed
# The local closed flag remains set when closing raw fails; the raw-close
# exception takes precedence if flushing also failed.
class FlushFailure(Exception):
    pass
class CloseFailure(Exception):
    pass
flush_error = FlushFailure('flush')
close_error = CloseFailure('close')
class CloseFails(Empty):
    def flush(self):
        raise flush_error
    def close(self):
        self.closes += 1
        raise close_error
raw = CloseFails()
raw.closes = 0
r = io.BufferedReader(raw, 4)
try:
    r.close()
    assert False
except CloseFailure as caught:
    assert caught is close_error
assert r.closed and not raw.closed and raw.closes == 1
r.close()
assert raw.closes == 1
# No buffer export crosses a raw callback, including for the caller's target.
class Destination(Empty):
    def readinto(self, b):
        self.destination.clear()
        b[0] = 65
        return 1
raw = Destination()
raw.destination = bytearray(2)
r = io.BufferedReader(raw, 4)
try:
    r.readinto(raw.destination)
    assert False
except ValueError:
    pass
assert raw.destination == bytearray()
r.close()
