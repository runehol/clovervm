import io
import errno

class Raw(io.RawIOBase):
    def __init__(self):
        self.data = bytearray()
        self.steps = []
    def writable(self):
        return True
    def write(self, data):
        data = bytes(data)
        if self.steps:
            count = self.steps.pop(0)
            if count is None:
                return None
        else:
            count = len(data)
        self.data.extend(data[:count])
        return count
raw = Raw()
w = io.BufferedWriter(raw, 4)
w.write(b'ab')
w.write(b'cd')
raw.steps = [2, None]
try:
    w.write(b'efgh')
    assert False
except BlockingIOError as error:
    assert error.characters_written == 2
assert raw.data == b'ab'
raw.steps = [1, None]
try:
    w.flush()
    assert False
except BlockingIOError as error:
    assert error.characters_written == 0
assert raw.data == b'abc'
raw.steps = [None]
try:
    w.write(b'gh')
    assert False
except BlockingIOError as error:
    assert error.characters_written == 1
w.flush()
assert raw.data == b'abcdefg'
assert w.write(b'h') == 1
w.flush()
assert raw.data == b'abcdefgh'
w.close()
# Large direct writes count both raw progress and accepted buffered bytes.
raw = Raw()
raw.steps = [2, None]
w = io.BufferedWriter(raw, 4)
try:
    w.write(b'abcdefghij')
    assert False
except BlockingIOError as error:
    assert error.characters_written == 6
assert raw.data == b'ab'
w.flush()
assert raw.data == b'abcdef'
assert w.write(b'ghij') == 4
w.flush()
assert raw.data == b'abcdefghij'
w.close()
# A raw-raised BlockingIOError during old-buffer flushing can still leave
# room to accept part of the new call. Its count excludes that old data.
class Blocked(Raw):
    def write(self, data):
        if self.blocked:
            raise BlockingIOError(errno.EAGAIN, 'raw blocked', 0)
        return Raw.write(self, data)
raw = Blocked()
raw.blocked = True
w = io.BufferedWriter(raw, 4)
w.write(b'ab')
try:
    w.write(b'cdefg')
    assert False
except BlockingIOError as error:
    assert error.characters_written == 2
raw.blocked = False
w.flush()
assert raw.data == b'abcd'
w.close()
