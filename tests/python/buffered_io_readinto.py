import io

class Stream(io.BufferedIOBase):
    def __init__(self):
        self.calls = []
        self.result = b'AB'
    def read(self, size):
        self.calls.append(['read', size])
        return self.result
    def read1(self, size):
        self.calls.append(['read1', size])
        return self.result
s = Stream()
for method in (io.BufferedIOBase.readinto, io.BufferedIOBase.readinto1):
    b = bytearray(b'....')
    assert method(s, b) == 2
    assert b == b'AB..'
assert s.calls == [['read', 4], ['read1', 4]]
s.result = b''
assert s.readinto(bytearray()) == 0
assert s.calls[-1] == ['read', 0]
for bad in (None, b'abc', [], 'abc'):
    before = len(s.calls)
    try:
        s.readinto(bad)
        assert False
    except TypeError:
        pass
    assert len(s.calls) == before
for bad in (None, bytearray(b'A'), 'abc', 3):
    s.result = bad
    b = bytearray(b'....')
    try:
        s.readinto(b)
        assert False
    except TypeError:
        pass
    assert b == b'....'
s.result = b'too long'
b = bytearray(b'....')
try:
    s.readinto(b)
    assert False
except ValueError:
    pass
assert b == b'....'
class BytesTrap(bytes):
    def __len__(self):
        raise AssertionError('stored bytes length')
    def __getitem__(self, key):
        raise AssertionError('stored bytes payload')
    def __iter__(self):
        raise AssertionError('stored bytes payload')
    def __bytes__(self):
        raise AssertionError('stored bytes payload')
s.result = BytesTrap(b'XY')
b = bytearray(b'....')
assert s.readinto(b) == 2 and b == b'XY..'
s.close()
class Failure(Exception):
    pass
error = Failure('read')
class Raising(io.BufferedIOBase):
    def read(self, size):
        raise error
    def read1(self, size):
        raise error
r = Raising()
for method in (io.BufferedIOBase.readinto, io.BufferedIOBase.readinto1):
    try:
        method(r, bytearray(3))
        assert False
    except Failure as caught:
        assert caught is error
r.close()
# The fallback dynamically resolves each read override.
def replacement(self, size):
    return b'Z'
Stream.read = replacement
b = bytearray(b'..')
assert s.readinto(b) == 1 and b == b'Z.'
