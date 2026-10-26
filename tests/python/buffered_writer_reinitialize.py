import io

class Raw(io.RawIOBase):
    def __init__(self):
        self.data = bytearray()
        self.probes = 0
    def writable(self):
        self.probes += 1
        return True
    def write(self, data):
        self.data.extend(bytes(data))
        return len(data)
class Index:
    def __index__(self):
        return self.value
class Failure(Exception):
    pass
failure = Failure('index')
class Raises:
    def __index__(self):
        raise failure
for value in (None, 'bad', 1.5, 2**100):
    raw = Raw()
    replacement = Raw()
    w = io.BufferedWriter(raw, 4)
    w.write(b'ab')
    if value == 2**100:
        try:
            w.__init__(replacement, value)
            assert False
        except OverflowError:
            pass
    else:
        try:
            w.__init__(replacement, value)
            assert False
        except TypeError:
            pass
    assert w.raw is raw and not w.closed and replacement.probes == 0
    assert w.write(b'c') == 1
    w.flush()
    assert raw.data == b'abc' and replacement.data == bytearray()
    w.close()
    replacement.close()
raw = Raw()
replacement = Raw()
w = io.BufferedWriter(raw, 4)
w.write(b'ab')
index = Index()
index.value = 'bad'
try:
    w.__init__(replacement, index)
    assert False
except TypeError:
    pass
try:
    w.__init__(replacement, Raises())
    assert False
except Failure as error:
    assert error is failure
assert w.raw is raw and replacement.probes == 0
w.close()
assert raw.data == b'ab'
replacement.close()
