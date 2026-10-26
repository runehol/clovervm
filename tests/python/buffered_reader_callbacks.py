import io
import errno

class Index:
    def __index__(self):
        self.calls += 1
        return self.value
class Raw(io.RawIOBase):
    def readable(self):
        return True
    def readinto(self, b):
        b[:2] = b'AB'
        return self.count
raw = Raw()
raw.count = 2
size = Index()
size.value = 4
size.calls = 0
r = io.BufferedReader(raw, size)
assert size.calls == 1
n = Index()
n.value = 1
n.calls = 0
assert r.read(n) == b'A' and n.calls == 1
r.close()
for value in (-1, 5, 2**100):
    raw = Raw()
    raw.count = value
    r = io.BufferedReader(raw, 4)
    try:
        r.read(1)
        assert False
    except OSError:
        pass
    # The failed callback must not leave the operation guard held.
    raw.count = 2
    assert r.read(1) == b'A'
    r.close()
for value in ('bad', 1.5):
    raw = Raw()
    raw.count = value
    r = io.BufferedReader(raw, 4)
    try:
        r.read(1)
        assert False
    except OSError:
        pass
    r.close()
class Failure(Exception):
    pass
error = Failure('raw callback')
class Raising(Raw):
    def readinto(self, b):
        raise error
r = io.BufferedReader(Raising(), 4)
try:
    r.read(1)
    assert False
except Failure as caught:
    assert caught is error
r.close()
class Interrupted(Raw):
    def readinto(self, b):
        self.calls += 1
        if self.calls == 1:
            raise InterruptedError(errno.EINTR, 'retry')
        b[0] = 65
        return 1
raw = Interrupted()
raw.calls = 0
r = io.BufferedReader(raw, 4)
assert r.read(1) == b'A' and raw.calls == 2
r.close()
# The readall optimization validates stored bytes, ignoring subclass hooks.
class BytesTrap(bytes):
    def __len__(self):
        raise AssertionError('stored bytes length')
    def __iter__(self):
        raise AssertionError('stored bytes payload')
class All(Raw):
    def readall(self):
        return self.data
raw = All()
raw.data = BytesTrap(b'abc')
r = io.BufferedReader(raw, 4)
assert r.read() is raw.data
raw.data = None
assert r.read() is None
raw.data = bytearray(b'bad')
try:
    r.read()
    assert False
except TypeError:
    pass
r.close()
# Raw streams need not inherit RawIOBase or provide readall.
class Plain:
    closed = False
    def __init__(self):
        self.parts = [b'ab', b'c', b'']
    def readable(self):
        return True
    def read(self):
        return self.parts.pop(0)
    def flush(self):
        pass
    def close(self):
        self.closed = True
r = io.BufferedReader(Plain(), 4)
assert r.read() == b'abc'
r.close()

# Errors converting a raw return count become OSError, without losing a raw
# callback's own exception when the callback raises directly.
class BadIndex:
    def __index__(self):
        raise error
raw = Raw()
raw.count = BadIndex()
r = io.BufferedReader(raw, 4)
try:
    r.read(1)
    assert False
except OSError:
    pass
raw.count = 2
assert r.read(1) == b'A'
r.close()
# Argument conversion precedes stream-state validation.
raw = Raw()
raw.count = 2
r = io.BufferedReader(raw, 4)
r.close()
for method in (io.BufferedReader.read, io.BufferedReader.read1, io.BufferedReader.peek):
    try:
        method(r, 'bad')
        assert False
    except TypeError:
        pass
for method in (io.BufferedReader.readinto, io.BufferedReader.readinto1):
    try:
        method(r, None)
        assert False
    except TypeError:
        pass
class ReadableProbe(Raw):
    def readable(self):
        self.probes += 1
        return True
raw = ReadableProbe()
raw.probes = 0
try:
    io.BufferedReader(raw, None)
    assert False
except TypeError:
    pass
assert raw.probes == 0
raw.close()
