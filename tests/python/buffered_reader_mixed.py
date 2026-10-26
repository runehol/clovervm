import io

class Raw(io.RawIOBase):
    def __init__(self):
        self.data = b'abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz'
        self.pos = 0
        self.calls = 0
    def readable(self):
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
    def readinto(self, b):
        self.calls += 1
        amount = min(len(b), 3)
        part = self.data[self.pos:self.pos + amount]
        b[:len(part)] = part
        self.pos += len(part)
        return len(part)
raw = Raw()
r = io.BufferedReader(raw, 4)
position = 0
state = 73
for step in range(300):
    state = (state * 17 + 31) % 997
    action = state % 7
    size = (state // 7) % 19
    before = raw.calls
    if action == 0:
        result = r.read(size)
        assert result == raw.data[position:position + size]
        position += len(result)
    elif action == 1:
        result = r.read1(size)
        assert len(result) <= size and raw.calls - before <= 1
        assert result == raw.data[position:position + len(result)]
        position += len(result)
    elif action == 2:
        b = bytearray(size)
        count = r.readinto(b)
        expected = raw.data[position:position + size]
        assert count == len(expected) and bytes(b[:count]) == expected
        assert b[count:] == bytearray(size - count)
        position += count
    elif action == 3:
        b = bytearray(size)
        count = r.readinto1(b)
        assert count <= size and raw.calls - before <= 1
        assert bytes(b[:count]) == raw.data[position:position + count]
        assert b[count:] == bytearray(size - count)
        position += count
    elif action == 4:
        position = state % 98
        assert r.seek(position) == position
    elif action == 5:
        assert r.tell() == position
    else:
        result = r.peek(size)
        assert result == raw.data[position:position + len(result)]
        assert raw.calls - before <= 1
assert r.tell() == position
r.close()
