import io
import errno

class Index:
    def __index__(self):
        self.calls += 1
        return self.value
class Raw(io.RawIOBase):
    def __init__(self):
        self.result = 2
        self.calls = 0
    def writable(self):
        return True
    def write(self, data):
        self.calls += 1
        return self.result
for value in (-1, 3, 2**100, 'bad', 1.5):
    raw = Raw()
    w = io.BufferedWriter(raw, 4)
    w.write(b'AB')
    raw.result = value
    try:
        w.flush()
        assert False
    except OSError:
        pass
    raw.result = 2
    w.flush()
    assert raw.calls == 2
    w.close()
raw = Raw()
count = Index()
count.value = 2
count.calls = 0
raw.result = count
size = Index()
size.value = 4
size.calls = 0
w = io.BufferedWriter(raw, size)
w.write(b'AB')
w.flush()
assert count.calls == 1 and size.calls == 1
w.close()
class Failure(Exception):
    pass
failure = Failure('raw write')
class Raising(Raw):
    def write(self, data):
        raise failure
raw = Raising()
w = io.BufferedWriter(raw, 4)
w.write(b'AB')
try:
    w.flush()
    assert False
except Failure as error:
    assert error is failure
# Replacing a raw method must be observed on the next flush.
def writes(self, data):
    self.output = bytes(data)
    return len(data)
Raising.write = writes
w.flush()
assert raw.output == b'AB'
w.close()
class Interrupted(Raw):
    def write(self, data):
        self.calls += 1
        if self.calls == 1:
            raise InterruptedError(errno.EINTR, 'retry')
        return len(data)
raw = Interrupted()
w = io.BufferedWriter(raw, 4)
w.write(b'AB')
w.flush()
assert raw.calls == 2
w.close()
class BytesTrap(bytes):
    def __len__(self):
        raise AssertionError('stored payload')
    def __getitem__(self, key):
        raise AssertionError('stored payload')
    def __iter__(self):
        raise AssertionError('stored payload')
    def __bytes__(self):
        raise AssertionError('stored payload')
raw = Raw()
w = io.BufferedWriter(raw, 4)
assert w.write(BytesTrap(b'AB')) == 2
w.flush()
w.close()
# Invalid inputs fail before checking closed stream state.
for value in (None, 'text', [], 3):
    try:
        w.write(value)
        assert False
    except TypeError:
        pass
# Acknowledged progress stays committed when a later raw write raises.
class Partial(Raw):
    def __init__(self):
        self.data = bytearray()
        self.stage = 0
    def write(self, data):
        data = bytes(data)
        self.stage += 1
        if self.stage == 1:
            self.data.extend(data[:1])
            return 1
        if self.stage == 2:
            raise failure
        self.data.extend(data)
        return len(data)
raw = Partial()
w = io.BufferedWriter(raw, 4)
w.write(b'ab')
w.write(b'cd')
try:
    w.flush()
    assert False
except Failure as error:
    assert error is failure
assert raw.data == b'a'
w.flush()
assert raw.data == b'abcd'
w.close()
# A failed count conversion does not consume the pending suffix.
class BadIndex:
    def __index__(self):
        raise failure
raw = Raw()
w = io.BufferedWriter(raw, 4)
w.write(b'AB')
raw.result = BadIndex()
try:
    w.flush()
    assert False
except OSError:
    pass
raw.result = 2
w.flush()
assert raw.calls == 2
w.close()
class IntTrap(int):
    def __index__(self):
        raise AssertionError('stored count')
    def __lt__(self, other):
        raise AssertionError('stored count')
    def __gt__(self, other):
        raise AssertionError('stored count')
raw = Raw()
raw.result = IntTrap(2)
w = io.BufferedWriter(raw, 4)
w.write(b'AB')
w.flush()
assert raw.calls == 1
w.close()
# The direct path must also bypass bytes-subclass slicing hooks.
raw = Raw()
w = io.BufferedWriter(raw, 4)
assert w.write(BytesTrap(b'abcdefghij')) == 10
w.flush()
w.close()
