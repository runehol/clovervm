import io

class Raw(io.RawIOBase):
    def __init__(self):
        self.steps = [0, 0]
        self.calls = []
        self.data = bytearray()
    def writable(self):
        return True
    def write(self, data):
        data = bytes(data)
        self.calls.append(data)
        if self.steps:
            return self.steps.pop(0)
        self.data.extend(data)
        return len(data)
# Retry the same non-empty direct write without treating zero as would-block.
raw = Raw()
w = io.BufferedWriter(raw, 4)
assert w.write(b'abcdef') == 6
assert raw.calls == [b'abcdef', b'abcdef', b'abcdef']
assert raw.data == b'abcdef'
w.close()
# The same rule applies when draining queued output.
raw = Raw()
w = io.BufferedWriter(raw, 4)
w.write(b'ab')
w.write(b'cd')
w.flush()
assert raw.calls == [b'abcd', b'abcd', b'abcd']
assert raw.data == b'abcd'
w.close()
