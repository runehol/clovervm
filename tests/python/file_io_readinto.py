import io
import os

path = "clover-readinto-" + str(os.getpid())
with io.FileIO(path, "wb") as f:
    f.write(b"abcdef")

buffer = bytearray(b"XXXXXXXXXX")
with io.FileIO(path) as f:
    assert f.readinto(buffer) == 6
    assert len(buffer) == 10
    assert bytes(buffer) == b"abcdefXXXX"
    assert f.tell() == 6
    assert f.readinto(buffer) == 0
    assert bytes(buffer) == b"abcdefXXXX"
    f.seek(0)
    small = bytearray(2)
    assert f.readinto(small) == 2
    assert small == b"ab" and f.tell() == 2
    assert f.readinto(bytearray()) == 0
    assert f.tell() == 2
    small.extend(b"X")
    assert f.readinto(small) == 3
    assert small == b"cde"
    # A temporary target remains valid across native entry/result handling.
    assert f.readinto(bytearray(8)) == 1

class Derived(io.FileIO):
    def readable(self):
        raise AssertionError('native readinto must use descriptor access flags')
with Derived(path) as f:
    assert f.readinto(buffer) == 6
    assert bytes(buffer) == b"abcdefXXXX"
    # Direct base calls retain the base's NotImplementedError default.
    try:
        io.RawIOBase.readinto(f, buffer)
        assert False
    except NotImplementedError:
        pass

class ConversionTrap:
    def __iter__(self):
        raise AssertionError('readinto must not convert its destination')
    def __len__(self):
        raise AssertionError('readinto must not convert its destination')

reader = io.FileIO(path)
writer = io.FileIO(path, 'wb')
closed = io.FileIO(path)
closed.close()
# Wrong buffer types are rejected before closed-state/readability errors.
for stream in (reader, writer, closed):
    for invalid in (b"immutable", None, [], 1, ConversionTrap()):
        try:
            stream.readinto(invalid)
            assert False
        except TypeError:
            pass

for target in (bytearray(), bytearray(b"XXXXX")):
    try:
        writer.readinto(target)
        assert False
    except io.UnsupportedOperation:
        pass
    try:
        closed.readinto(target)
        assert False
    except ValueError:
        pass
    assert target == b"" or target == b"XXXXX"
reader.close()
writer.close()

try:
    io.FileIO.readinto(object(), buffer)
    assert False
except TypeError:
    pass
try:
    closed.readinto()
    assert False
except TypeError:
    pass
try:
    closed.readinto(buffer=buffer)
    assert False
except TypeError:
    pass
os.unlink(path)
