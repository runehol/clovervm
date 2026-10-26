import io

class Failure(Exception):
    pass
failure = Failure('flush')
class Raw(io.RawIOBase):
    def __init__(self):
        self.fail = False
        self.closes = 0
        self.data = bytearray()
    def writable(self):
        return True
    def seekable(self):
        return True
    def seek(self, offset, whence=0):
        return 0
    def write(self, data):
        if self.fail:
            raise failure
        self.data.extend(bytes(data))
        return len(data)
    def close(self):
        self.closes += 1
        io.RawIOBase.close(self)
raw = Raw()
w = io.BufferedWriter(raw, 4)
w.write(b'ab')
raw.fail = True
try:
    w.close()
    assert False
except Failure as error:
    assert error is failure
assert raw.closes == 1 and raw.closed and w.closed
w.close()
assert raw.closes == 1
class Writer(io.BufferedWriter):
    def flush(self):
        self.flushed = True
        io.BufferedWriter.flush(self)
raw = Raw()
w = Writer(raw, 4)
w.write(b'ab')
w.close()
assert w.flushed and raw.data == b'ab' and raw.closed
# Failed detach/seek preserves pending bytes and releases the operation guard.
raw = Raw()
w = io.BufferedWriter(raw, 4)
w.write(b'xy')
raw.fail = True
try:
    w.detach()
    assert False
except Failure:
    pass
assert w.raw is raw and not w.closed
try:
    w.seek(0)
    assert False
except Failure:
    pass
raw.fail = False
assert w.detach() is raw and raw.data == b'xy'
raw.close()
