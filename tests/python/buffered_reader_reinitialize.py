import io


class Raw(io.RawIOBase):
    def __init__(self):
        self.pos = 0
        self.probes = 0

    def readable(self):
        self.probes += 1
        return True

    def readinto(self, buffer):
        data = b'abcdef'[self.pos:self.pos + len(buffer)]
        buffer[:len(data)] = data
        self.pos += len(data)
        return len(data)


class BadIndex:
    def __index__(self):
        return 'bad'


class Failure(Exception):
    pass


failure = Failure('index')


class RaisingIndex:
    def __index__(self):
        raise failure


for value in (None, 'bad', 1.5, 2**100, BadIndex(), RaisingIndex()):
    raw = Raw()
    replacement = Raw()
    reader = io.BufferedReader(raw, 4)
    assert reader.read(1) == b'a'
    try:
        reader.__init__(replacement, value)
        assert False
    except TypeError:
        assert value != 2**100 and not isinstance(value, RaisingIndex)
    except OverflowError:
        assert value == 2**100
    except Failure as caught:
        assert caught is failure and isinstance(value, RaisingIndex)
    assert reader.raw is raw and not reader.closed
    assert replacement.probes == 0 and replacement.pos == 0
    assert reader.read(3) == b'bcd' and raw.pos == 4
    assert reader.read() == b'ef'
    reader.close()
    assert raw.closed and not replacement.closed
    replacement.close()
