import io

class Reader(io.RawIOBase):
    def __init__(self, parts):
        self.parts = parts
        self.requests = []
    def read(self, size=-1):
        self.requests.append(size)
        return self.parts.pop(0)
for case in (([b'a', b'bc', b''], b'abc'),
                        ([b'a', None], b'a'), ([None], None), ([b''], b'')):
    r = Reader(case[0])
    assert r.readall() == case[1]
    assert r.parts == []
    assert r.requests and r.requests[0] == 128 * 1024
    r.close()
for value in (bytearray(b'abc'), '', 0, [], False):
    r = Reader([value])
    try:
        r.readall()
        assert False
    except TypeError:
        pass
    r.close()

class BytesTrap(bytes):
    def __len__(self):
        raise AssertionError('use stored bytes length')
    def __iter__(self):
        raise AssertionError('use stored bytes payload')
    def __getitem__(self, item):
        raise AssertionError('use stored bytes payload')
    def __bytes__(self):
        raise AssertionError('use stored bytes payload')
    def __bool__(self):
        raise AssertionError('use stored bytes length')
r = Reader([BytesTrap(b'abc'), BytesTrap(b'd'), BytesTrap(b'')])
assert r.readall() == b'abcd'
r.close()

class IntoOnly(io.RawIOBase):
    def __init__(self):
        self.parts = [b'a', b'bc', b'']
    def readinto(self, buffer):
        part = self.parts.pop(0)
        buffer[:len(part)] = part
        return len(part)
r = IntoOnly()
assert r.readall() == b'abc'
r.close()
r = IntoOnly()
assert r.read() == b'abc'
r.close()

# Each loop iteration resolves the current read method.
def final_read(self, size):
    return b''
class Mutating(io.RawIOBase):
    def read(self, size):
        Mutating.read = final_read
        return b'a'
r = Mutating()
assert r.readall() == b'a'
r.close()
