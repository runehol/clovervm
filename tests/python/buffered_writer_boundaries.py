import io

class Failure(Exception):
    pass
failure = Failure('raw write')
class Raw(io.RawIOBase):
    def __init__(self):
        self.data = bytearray()
        self.calls = []
        self.steps = []
    def writable(self):
        return True
    def write(self, data):
        data = bytes(data)
        self.calls.append(data)
        if self.steps:
            result = self.steps.pop(0)
            if result is failure:
                raise failure
            if result is None:
                return None
        else:
            result = len(data)
        self.data.extend(data[:result])
        return result
# A full-capacity input reaches raw immediately, exposing its error now.
raw = Raw()
raw.steps = [failure]
w = io.BufferedWriter(raw, 4)
try:
    w.write(b'abcd')
    assert False
except Failure as error:
    assert error is failure
assert raw.calls == [b'abcd'] and raw.data == bytearray()
w.close()
# A direct partial write leaving exactly one buffer must try raw again.
raw = Raw()
raw.steps = [2, failure]
w = io.BufferedWriter(raw, 4)
try:
    w.write(b'abcdef')
    assert False
except Failure as error:
    assert error is failure
assert raw.calls == [b'abcdef', b'cdef'] and raw.data == b'ab'
w.close()
# When that last full-buffer attempt would block, all remaining bytes fit.
raw = Raw()
raw.steps = [2, None]
w = io.BufferedWriter(raw, 4)
assert w.write(b'abcdef') == 6
assert raw.calls == [b'abcdef', b'cdef'] and raw.data == b'ab'
w.flush()
assert raw.data == b'abcdef'
w.close()
raw = Raw()
raw.steps = [None]
w = io.BufferedWriter(raw, 4)
assert w.write(b'abcd') == 4
assert raw.calls == [b'abcd'] and raw.data == bytearray()
w.flush()
assert raw.data == b'abcd'
w.close()
# One byte beyond capacity cannot be accepted in full on would-block.
raw = Raw()
raw.steps = [None]
w = io.BufferedWriter(raw, 4)
try:
    w.write(b'abcde')
    assert False
except BlockingIOError as error:
    assert error.characters_written == 4
w.flush()
assert raw.data == b'abcd'
w.close()
# A small input may still finish filling an existing buffer without raw I/O.
raw = Raw()
w = io.BufferedWriter(raw, 4)
assert w.write(b'ab') == 2 and w.write(b'cd') == 2
assert raw.calls == []
w.close()
assert raw.data == b'abcd'
# After a partial failed flush, prefix space is reclaimed only after another
# attempt to flush the old suffix. A newly raised raw error is not deferred.
raw = Raw()
w = io.BufferedWriter(raw, 4)
w.write(b'ab')
w.write(b'cd')
raw.steps = [1, None]
try:
    w.flush()
    assert False
except BlockingIOError:
    pass
assert raw.data == b'a'
raw.steps = [failure]
try:
    w.write(b'e')
    assert False
except Failure as error:
    assert error is failure
assert raw.calls[-1] == b'bcd'
w.close()
assert raw.data == b'abcd'
