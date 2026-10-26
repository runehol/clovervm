import io


class Failure(Exception):
    pass


class Raw(io.RawIOBase):
    def __init__(self, parts):
        self.parts = parts
        self.calls = []

    def readable(self):
        return True

    def readinto(self, buffer):
        self.calls.append(len(buffer))
        part = self.parts.pop(0)
        if isinstance(part, BaseException):
            # Earlier successful transfers must already be visible even
            # inside the callback which is about to fail.
            assert self.destination == self.expected
            raise part
        if part is None:
            return None
        buffer[:len(part)] = part
        return len(part)


# Both direct transfers and internal refills must commit partial progress.
for case in (([b'abcd'], b'........', b'abcd....'),
             ([b'a', b'b'], b'...', b'ab.')):
    parts = case[0]
    expected = case[2]
    error = Failure('later raw read')
    raw = Raw(parts + [error, b'Z'])
    reader = io.BufferedReader(raw, 4)
    raw.destination = bytearray(case[1])
    raw.expected = expected
    try:
        reader.readinto(raw.destination)
        assert False
    except Failure as caught:
        assert caught is error
    assert raw.destination == expected
    # The operation guard is released after the exception.
    assert reader.read(1) == b'Z'
    reader.close()


# readinto1 can copy buffered bytes before its one direct read fails.
for method in (io.BufferedReader.readinto, io.BufferedReader.readinto1):
    error = Failure('raw read after read-ahead')
    raw = Raw([b'abcd', error, b'Z'])
    reader = io.BufferedReader(raw, 4)
    assert reader.read(1) == b'a'
    raw.destination = bytearray(b'........')
    raw.expected = b'bcd.....'
    try:
        method(reader, raw.destination)
        assert False
    except Failure as caught:
        assert caught is error
    assert raw.destination == raw.expected
    assert reader.read(1) == b'Z'
    reader.close()


# An exact-capacity request refills internal storage. Short transfers are not
# EOF; readinto1 performs one callback, while readinto keeps filling.
for case in ((io.BufferedReader.readinto, b'abcd', 2),
             (io.BufferedReader.readinto1, b'ab..', 1)):
    method = case[0]
    expected = case[1]
    calls = case[2]
    raw = Raw([b'ab', b'cd'])
    reader = io.BufferedReader(raw, 4)
    destination = bytearray(b'....')
    count = method(reader, destination)
    assert count == (4 if method is io.BufferedReader.readinto else 2)
    assert destination == expected and len(raw.calls) == calls
    reader.close()


# Would-block preserves a committed prefix and the unmodified suffix.
raw = Raw([b'ab', None])
reader = io.BufferedReader(raw, 4)
destination = bytearray(b'........')
assert reader.readinto(destination) == 2
assert destination == b'ab......'
reader.close()


# Invalid negative sizes are rejected before raw I/O or buffer consumption.
raw = Raw([b'abcd'])
reader = io.BufferedReader(raw, 4)
for size in (-2, -100):
    try:
        reader.read(size)
        assert False
    except ValueError:
        pass
assert raw.calls == []
assert reader.read(1) == b'a'
try:
    reader.read(-2)
    assert False
except ValueError:
    pass
assert reader.read(3) == b'bcd' and raw.calls == [4]
reader.close()


class NonSeekable(Raw):
    def seek(self, offset, whence=0):
        raise AssertionError('non-seekable raw seek must not run')


raw = NonSeekable([b'abcd'])
reader = io.BufferedReader(raw, 4)
assert reader.read(1) == b'a'
for whence in (0, 1, 2):
    try:
        reader.seek(10, whence)
        assert False
    except io.UnsupportedOperation:
        pass
assert reader.read(3) == b'bcd'
reader.close()


class SeekabilityFailure(Raw):
    def seekable(self):
        if self.fail:
            raise self.error
        return False


raw = SeekabilityFailure([b'abcd'])
raw.error = Failure('seekable')
raw.fail = False
reader = io.BufferedReader(raw, 4)
assert reader.read(1) == b'a'
raw.fail = True
try:
    reader.seek(10)
    assert False
except Failure as caught:
    assert caught is raw.error
assert reader.read(3) == b'bcd'
reader.close()
