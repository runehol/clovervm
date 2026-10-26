import io
import errno

class Index:
    def __index__(self):
        self.calls += 1
        return errno.EINTR
class BadIndex:
    def __index__(self):
        raise RuntimeError('errno conversion')
class IntTrap(int):
    def __eq__(self, other):
        raise AssertionError('errno uses stored integer payload')
class Reader(io.RawIOBase):
    def __init__(self, error):
        self.error = error
        self.calls = 0
    def read(self, size):
        self.calls += 1
        if self.calls == 1:
            raise self.error
        if self.calls == 2:
            return b'ok'
        return b''
for code in (errno.EINTR, IntTrap(errno.EINTR), Index()):
    error = OSError('retry')
    error.errno = code
    r = Reader(error)
    if isinstance(code, Index):
        code.calls = 0
    assert r.readall() == b'ok' and r.calls == 3
    if isinstance(code, Index):
        assert code.calls == 1
    r.close()
for code in (None, errno.EBADF, 2**100, 'bad', BadIndex()):
    error = OSError('propagate')
    error.errno = code
    r = Reader(error)
    try:
        r.readall()
        assert False
    except OSError as caught:
        assert caught is error
    assert r.calls == 1
    r.close()

# A later error must propagate rather than return already accumulated data.
class Failure(Exception):
    pass
error = Failure('after data')
class Later(io.RawIOBase):
    def read(self, size):
        self.calls += 1
        if self.calls == 1:
            return b'first'
        raise error
r = Later()
r.calls = 0
try:
    r.readall()
    assert False
except Failure as caught:
    assert caught is error
assert r.calls == 2
r.close()
