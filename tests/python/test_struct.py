"""Independent byte fixtures and public API assertions for the integer slice."""

import struct
import sys


class Format(str):
    def __len__(self):
        raise AssertionError

    def __getitem__(self, key):
        raise AssertionError

    def encode(self, *args):
        raise AssertionError


class Payload(bytes):
    def __bytes__(self):
        raise AssertionError

    def __len__(self):
        raise AssertionError

    def __getitem__(self, key):
        raise AssertionError

    def decode(self, *args):
        raise AssertionError


class Number(int):
    def __index__(self):
        raise AssertionError

    def to_bytes(self, *args, **kwargs):
        raise AssertionError


def fixture(format, values, data):
    spec = struct.Struct(format=format)
    assert spec.size == struct.calcsize(format) == len(data)
    assert spec.pack(*values) == struct.pack(format, *values) == data
    assert spec.unpack(data) == struct.unpack(format, data) == values
    assert spec.unpack(Payload(data)) == values
    assert struct.unpack(format, bytearray(data)) == values


def raises(kind, call, *args, **kwargs):
    try:
        call(*args, **kwargs)
    except kind as exc:
        return str(exc)
    raise AssertionError


fixture(">B2h3xI", (171, -2, 258, 2309737967),
        b"\xab\xff\xfe\x01\x02\x00\x00\x00\x89\xab\xcd\xef")
fixture("<bH2xq", (-128, 4660, -2),
        b"\x80\x34\x12\x00\x00\xfe\xff\xff\xff\xff\xff\xff\xff")
fixture("!2H0I", (4660, 43981), b"\x12\x34\xab\xcd")
fixture(">Qq", ((1 << 64) - 1, -(1 << 63)),
        b"\xff\xff\xff\xff\xff\xff\xff\xff\x80\x00\x00\x00\x00\x00\x00\x00")
fixture(">2x 01H\t 0q\v 2x", (258,), b"\x00\x00\x01\x02\x00\x00")
fixture("x2b0x", (-1, 127), b"\x00\xff\x7f")
fixture("@2xB", (True,), b"\x00\x00\x01")
fixture("", (), b"")
fixture("@", (), b"")
fixture(" \t\n\r\v\f", (), b"")
fixture(">2x0x", (), b"\x00\x00")
assert struct.unpack(">B3xB", b"\x01abc\x02") == (1, 2)
native = b"\x02\x01" if sys.byteorder == "little" else b"\x01\x02"
fixture("=h", (258,), native)
assert struct.calcsize("=lL") == 8
assert struct.pack(Format(">H"), Number(258)) == b"\x01\x02"
assert struct.Struct(Payload(b">H")).format == ">H"
assert struct.pack(Payload(b">H"), 258) == b"\x01\x02"
assert struct.unpack(">B", b"\x01")[0].__class__ is int

spec = struct.Struct(">H")
assert raises(struct.error, spec.__init__, ">z") == "bad char in struct format"
assert spec.format == ">H" and spec.size == 2 and spec.pack(258) == b"\x01\x02"
raises(AttributeError, setattr, spec, "format", ">B")
raises(AttributeError, setattr, spec, "size", 1)
raises(AttributeError, delattr, spec, "size")
spec.__init__(format=b"<I")
assert spec.format == "<I" and spec.size == 4 and spec.pack(258) == b"\x02\x01\x00\x00"
raises(TypeError, spec.pack, value=1)
raises(TypeError, spec.unpack, buffer=b"\x00\x00\x00\x00")
raises(TypeError, struct.pack, format=">H")
raises(TypeError, struct.unpack, ">H", buffer=b"\x00\x00")
raises(TypeError, struct.calcsize, format=">H")

assert raises(struct.error, struct.pack, ">0z", "wrong") == "bad char in struct format"
assert raises(struct.error, struct.unpack, ">z", None) == "bad char in struct format"
assert raises(struct.error, struct.calcsize, ">z\x00") == "embedded null character"
assert raises(struct.error, struct.calcsize, ">2") == "repeat count given without format specifier"
assert raises(struct.error, struct.calcsize, ">2 H") == "bad char in struct format"
assert raises(struct.error, struct.calcsize, " >H") == "bad char in struct format"
assert raises(struct.error, struct.pack, ">H", "wrong", 0) == "pack expected 1 items for packing (got 2)"
assert raises(struct.error, struct.pack, ">HH", "wrong", -1) == "required argument is not an integer"
assert raises(struct.error, struct.pack, ">HH", -1, "wrong") == "'H' format requires 0 <= number <= 65535"
raises(TypeError, struct.unpack, ">H", [0, 1])
assert raises(struct.error, struct.unpack, ">H", b"") == "unpack requires a buffer of 2 bytes"
assert raises(struct.error, struct.unpack, ">H", b"123") == "unpack requires a buffer of 2 bytes"
raises(TypeError, struct.Struct, bytearray(b">H"))
invalid_text = ">z\x00\u00a0\u00e9H\u00e9"
for text in (invalid_text, Format(invalid_text)):
    try:
        struct.calcsize(text)
    except UnicodeEncodeError as exc:
        assert exc.encoding == "ascii"
        assert exc.object is text
        assert exc.start == 3 and exc.end == 5
    else:
        raise AssertionError

max_size = (1 << 63) - 1
assert struct.calcsize(">9223372036854775807x") == max_size
assert struct.Struct(">9223372036854775806xB").size == max_size
raises(OverflowError, struct.pack, ">9223372036854775807x")
assert raises(struct.error, struct.pack, ">9223372036854775806xB") == (
    "pack expected 1 items for packing (got 0)")
for text in (">9223372036854775808", ">9223372036854775808z",
             ">9223372036854775808x", ">9223372036854775807xx",
             ">1152921504606846976q", ">9223372036854775807Bx"):
    assert raises(struct.error, struct.calcsize, text) == "total struct size too long"

if sys.implementation.name == "clovervm":
    over_storage = sys.maxsize + 1
    pad_format = ">" + str(over_storage) + "x"
    assert struct.calcsize(pad_format) == over_storage
    raises(OverflowError, struct.pack, pad_format)
    for text in ("bI", "@b0I", "@l", ">s", ">?", "@n", "<f", ">0d"):
        raises(struct.error, struct.calcsize, text)
    for call in (struct.Struct, struct.calcsize, struct.pack):
        assert raises(struct.error, call, b">\xff") == "bad char in struct format"
    assert raises(struct.error, struct.unpack, b">\xff", None) == "bad char in struct format"

    class Index:
        def __index__(self):
            raise AssertionError

    assert raises(struct.error, struct.pack, ">H", Index()) == "required argument is not an integer"

    padding_requests = []

    def record_padding(count):
        padding_requests.append(count)
        return bytes(count)

    padded = struct.Struct(">3xB4xB2x")
    struct.bytes = record_padding
    try:
        raises(struct.error, padded.pack, 1, "wrong")
        raises(struct.error, padded.pack, 1, 256)
        assert padding_requests == []
        assert padded.pack(1, 2) == b"\x00\x00\x00\x01\x00\x00\x00\x00\x02\x00\x00"
        assert padding_requests == [3, 4, 2]
    finally:
        del struct.bytes
