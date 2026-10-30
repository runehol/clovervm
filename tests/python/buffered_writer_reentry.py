# CloverVM's explicit operation guard and accepted computed-property limits.
import io

class Raw(io.RawIOBase):
    def __init__(self):
        self.action = 6
        self.data = bytearray()
    def writable(self):
        return True
    def write(self, data):
        if self.action == 0:
            self.writer.write(b'!')
        elif self.action == 1:
            self.writer.flush()
        elif self.action == 2:
            self.writer.seek(0)
        elif self.action == 3:
            self.writer.detach()
        elif self.action == 4:
            self.writer.close()
        elif self.action == 5:
            self.writer.__init__(self, 4)
        self.data.extend(bytes(data))
        return len(data)
for action in (0, 1, 2, 3, 4, 5):
    raw = Raw()
    w = io.BufferedWriter(raw, 4)
    raw.writer = w
    w.write(b'ab')
    raw.action = action
    try:
        w.flush()
        assert False
    except RuntimeError:
        pass
    raw.action = 6
    w.flush()
    assert raw.data == b'ab'
    w.close()
raw = Raw()
w = io.BufferedWriter(raw, 4)
raw.close()
assert not w.closed
assert not hasattr(w, 'name') and not hasattr(w, 'mode')
w.close()
assert w.closed
class FlushFailure(Exception):
    pass
class CloseFailure(Exception):
    pass
flush_error = FlushFailure('flush')
close_error = CloseFailure('close')
class CloseFails(Raw):
    def write(self, data):
        raise flush_error
    def close(self):
        raise close_error
raw = CloseFails()
w = io.BufferedWriter(raw, 4)
w.write(b'ab')
try:
    w.close()
    assert False
except CloseFailure as error:
    assert error is close_error
assert w.closed and not raw.closed
w.close()
# Reinitialization uses the existing base flag but cannot bypass an active raw
# operation's guard (tested above).
w.__init__(Raw(), 4)
assert not w.closed
w.close()
# Mutable input is snapshotted before any raw callback can change its contents.
class Mutating(Raw):
    def write(self, data):
        self.source[:] = b'XXXXXXXXXX'
        self.data.extend(bytes(data[:2]))
        return min(2, len(data))
raw = Mutating()
raw.source = bytearray(b'abcdefghij')
w = io.BufferedWriter(raw, 4)
assert w.write(raw.source) == 10
w.flush()
assert raw.data == b'abcdefghij' and raw.source == b'XXXXXXXXXX'
w.close()
