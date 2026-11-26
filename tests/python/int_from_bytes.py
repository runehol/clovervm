# Shared behavior checked against CPython 3.14.

def raises(error_type, call, *args, **kwargs):
    try:
        call(*args, **kwargs)
    except error_type:
        return
    raise AssertionError("expected decoding to fail")


padding = bytearray()
for unused in range(257):
    padding.append(255)
all_ones = bytes(padding)

for case in (
    (b"", 0, 0),
    (b"\xff", 255, -1),
    (b"\x00\x80", 128, 128),
    (b"\xff\x00", 65280, -256),
    (b"\xff\x7f", 65407, -129),
    (b"\x01\x00\x00\x00\x01", (1 << 32) + 1, (1 << 32) + 1),
    (b"\xff\x00\x00\x00\x00", 255 << 32, -(1 << 32)),
    (b"\x80" + bytes(7), 1 << 63, -(1 << 63)),
    (all_ones[:17], (1 << 136) - 1, -1),
):
    data = case[0]
    unsigned = case[1]
    signed = case[2]
    assert int.from_bytes(data) == unsigned
    assert int.from_bytes(data[::-1], "little") == unsigned
    assert int.from_bytes(data, signed=True) == signed
    assert int.from_bytes(data[::-1], "little", signed=True) == signed

for value in (-(1 << 58) - 1, -(1 << 58), -(1 << 58) + 1,
              (1 << 58) - 2, (1 << 58) - 1, 1 << 58,
              -(1 << 255) + 1, (1 << 255) + 1):
    for order in ("big", "little"):
        data = value.to_bytes(40, order, signed=True)
        result = int.from_bytes(data, order, signed=True)
        assert result == value
        assert result.__class__ is int

assert int.from_bytes(bytes(257)) == 0
assert int.from_bytes(all_ones + b"\x00", signed=True) == -256
assert int.from_bytes(bytes=bytearray(b"\x12\x34"), byteorder="little") == 13330
for source in ([True, 255], (True, 255), iter([True, 255]), {True: 0, 255: 0}):
    assert int.from_bytes(source) == 511
for source in (0, True, "", None):
    raises(TypeError, int.from_bytes, source)
for item in (-1, 256, 1 << 100):
    raises(ValueError, int.from_bytes, [item])
raises(TypeError, int.from_bytes, [1.0])
raises(TypeError, int.from_bytes)
raises(TypeError, int.from_bytes, b"", bytes=b"")
raises(TypeError, int.from_bytes, b"", "big", True)


events = []


class Binary(bytes):
    def __iter__(self):
        raise AssertionError("bytes storage should be read directly")


class WithBytes(Binary):
    def __bytes__(self):
        events.append("bytes")
        return Binary(b"\xff")


class BadBytes:
    def __bytes__(self):
        return bytearray(b"A")

    def __iter__(self):
        raise AssertionError("a bad __bytes__ result should fail")


class RaisingBytes:
    def __bytes__(self):
        raise ValueError("from __bytes__")

    def __iter__(self):
        raise AssertionError("a raising __bytes__ should fail")


class Index:
    def __index__(self):
        events.append("index")
        return 255


class BadIndex:
    def __index__(self):
        return "255"


class InterruptIndex:
    def __index__(self):
        raise StopIteration


class Source:
    def __iter__(self):
        events.append("iter")
        return iter([True, Index()])


class Items(list):
    def __iter__(self):
        return iter([42])


class IntItem(int):
    def __index__(self):
        raise AssertionError("int payload already supplies the index")


assert int.from_bytes(Binary(b"\x12\x34")) == 4660
assert int.from_bytes(Items([9])) == 42
assert int.from_bytes([IntItem(42)]) == 42
assert int.from_bytes(Source()) == 511
assert events == ["iter", "index"]

def replacement_bytes(self):
    return b"\x0c"


Source.__bytes__ = replacement_bytes
assert int.from_bytes(Source()) == 12
assert events == ["iter", "index"]
del Source.__bytes__
assert int.from_bytes(Source()) == 511

raises(TypeError, int.from_bytes, BadBytes())
raises(ValueError, int.from_bytes, RaisingBytes())
raises(TypeError, int.from_bytes, [BadIndex()])
raises(StopIteration, int.from_bytes, [InterruptIndex()])
iterator = iter([256, 42])
raises(ValueError, int.from_bytes, iterator)
assert next(iterator) == 42


class Order(str):
    def __eq__(self, other):
        raise AssertionError("byteorder is checked by string contents")


class Signed:
    def __bool__(self):
        events.append("signed")
        return True


class RaisingSigned:
    def __bool__(self):
        events.append("signed")
        raise RuntimeError


events.clear()
raises(TypeError, int.from_bytes, WithBytes(b"A"), None, signed=Signed())
assert events == []
raises(ValueError, int.from_bytes, WithBytes(b"A"), "big\x00", signed=Signed())
assert events == ["signed"]
events.clear()
raises(RuntimeError, int.from_bytes, WithBytes(b"A"), "bad", signed=RaisingSigned())
assert events == ["signed"]
events.clear()
assert int.from_bytes(WithBytes(b"A"), Order("little"), signed=Signed()) == -1
assert events == ["signed", "bytes"]

mutable = bytearray(b"\xff")


class MutatingSigned:
    def __len__(self):
        mutable.append(0)
        return 1


assert int.from_bytes(mutable, signed=MutatingSigned()) == -256


class Plain(int):
    pass


class Inherited(Plain):
    pass


saved = Inherited.from_bytes
assert saved(b"").__class__ is Inherited
assert Inherited(0).from_bytes(b"\xff", signed=True).__class__ is Inherited
assert Plain.from_bytes(all_ones[:17]) == (1 << 136) - 1
assert bool.from_bytes(b"\x00") is False
assert True.from_bytes(b"\xff") is True


class Custom(Plain):
    def __new__(cls, value):
        assert value.__class__ is int
        events.append(value)
        return int.__new__(cls, value + 1)


class Alternate(int):
    def __new__(cls, value):
        return (cls, value)


events.clear()
custom = Custom.from_bytes(bytes=b"\x01\x02")
assert custom.__class__ is Custom
assert custom == 259
assert events == [258]
assert Alternate.from_bytes(b"\xff", signed=True) == (Alternate, -1)
