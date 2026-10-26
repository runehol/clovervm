import io

class Raw(io.RawIOBase):
    def __init__(self):
        self.data = bytearray()
        self.pos = 0
        self.calls = 0
    def writable(self):
        return True
    def seekable(self):
        return True
    def tell(self):
        return self.pos
    def seek(self, offset, whence=0):
        if whence == 0:
            self.pos = offset
        elif whence == 1:
            self.pos += offset
        else:
            self.pos = len(self.data) + offset
        return self.pos
    def write(self, data):
        self.calls += 1
        if self.calls % 7 == 0:
            return 0
        if self.calls % 5 == 0:
            return None
        count = min(len(data), 3)
        if self.pos > len(self.data):
            self.data.extend(bytearray(self.pos - len(self.data)))
        self.data[self.pos:self.pos + count] = bytes(data[:count])
        self.pos += count
        return count
raw = Raw()
w = io.BufferedWriter(raw, 4)
expected = bytearray()
position = 0
state = 61
for step in range(200):
    state = (state * 17 + 31) % 997
    action = state % 4
    if action == 0:
        target = state % 41
        while True:
            try:
                assert w.seek(target) == target
                break
            except BlockingIOError as error:
                assert error.characters_written == 0
        position = target
    elif action == 1:
        while True:
            try:
                w.flush()
                break
            except BlockingIOError as error:
                assert error.characters_written == 0
        assert raw.data == expected
    else:
        data = b'abcdefg'[:state % 8]
        offset = 0
        while offset < len(data):
            try:
                accepted = w.write(data[offset:])
            except BlockingIOError as error:
                accepted = error.characters_written
            assert 0 <= accepted <= len(data) - offset
            if accepted:
                if position > len(expected):
                    expected.extend(bytearray(position - len(expected)))
                expected[position:position + accepted] = data[offset:offset + accepted]
                offset += accepted
                position += accepted
    assert w.tell() == position
while True:
    try:
        w.flush()
        break
    except BlockingIOError:
        pass
assert raw.data == expected
w.close()
