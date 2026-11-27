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


def buffer_fixture(pack_into, unpack_from, iter_unpack):
    original = b"abcdef"
    expected = b"a\x2a\x00\x00\x07f"
    for offset in (1, -5, True, Number(-5)):
        buf = bytearray(original)
        assert pack_into(buf, offset, 42, 7) is None
        assert buf == expected
        assert unpack_from(buf, offset) == (42, 7)
        assert unpack_from(Payload(expected), offset) == (42, 7)
    assert unpack_from(b"\x2a@@\x07extra") == (42, 7)
    assert raises(struct.error, pack_into) == "pack_into expected buffer argument"
    assert raises(struct.error, pack_into, None) == "pack_into expected offset argument"
    assert raises(struct.error, pack_into, None, None, 1) == (
        "pack_into expected 2 items for packing (got 1)")
    raises(TypeError, pack_into, original, None, 1, 2)
    raises(TypeError, pack_into, bytearray(), None, 1, 2)
    raises(TypeError, unpack_from, [])
    raises(TypeError, unpack_from, original, 1.0)
    raises(TypeError, pack_into, original, max_size + 1, 1, 2)
    raises(TypeError, unpack_from, [], max_size + 1)
    for offset in (-max_size - 2, max_size + 1):
        raises(IndexError, pack_into, bytearray(), offset, 1, 2)
        raises(OverflowError, unpack_from, original, offset)
    assert raises(struct.error, pack_into, bytearray(b"x"), -2, 1, 2) == (
        "no space to pack 4 bytes at offset -2")
    assert raises(struct.error, unpack_from, b"x", -2) == (
        "not enough data to unpack 4 bytes at offset -2")
    for offset in (-7, -max_size - 1):
        message = "offset " + str(offset) + " out of range for 6-byte buffer"
        assert raises(struct.error, pack_into, bytearray(original), offset, 1, 2) == message
        assert raises(struct.error, unpack_from, original, offset) == message
    buf = bytearray(original)
    assert raises(struct.error, pack_into, buf, max_size, "bad", -1) == (
        "pack_into requires a buffer of at least 9223372036854775811 bytes " +
        "for packing 4 bytes at offset 9223372036854775807 (actual buffer size is 6)")
    assert buf == original
    assert raises(struct.error, pack_into, buf, 1, 42, 256) == (
        "'B' format requires 0 <= number <= 255")
    assert buf == b"a\x2a\x00\x00\x00f"
    assert raises(struct.error, pack_into, buf, 1, "bad", 7) == (
        "required argument is not an integer")
    assert buf == b"a\x00\x00\x00\x00f"

    records = iter_unpack(Payload(b"\x01ab\x02\x03cd\x04"))
    assert iter(records) is records and records.__length_hint__() == 2
    assert next(records) == (1, 2) and records.__length_hint__() == 1
    assert next(records) == (3, 4) and records.__length_hint__() == 0
    raises(StopIteration, next, records)
    assert records.__length_hint__() == 0
    raises(StopIteration, next, records)
    assert list(iter_unpack(b"")) == []
    assert raises(struct.error, iter_unpack, b"abcde") == (
        "iterative unpacking requires a buffer of a multiple of 4 bytes")


def check_buffer_operations():
    def pack_record(*args):
        return struct.pack_into(">B2xB", *args)


    def unpack_record(*args):
        return struct.unpack_from(">B2xB", *args)


    def iter_records(*args):
        return struct.iter_unpack(">B2xB", *args)


    spec = struct.Struct(">B2xB")
    buffer_fixture(pack_record, unpack_record, iter_records)
    buffer_fixture(spec.pack_into, spec.unpack_from, spec.iter_unpack)
    assert spec.unpack_from(buffer=b"0\x01ab\x02", offset=1) == (1, 2)
    assert struct.unpack_from(">B2xB", buffer=b"0\x01ab\x02", offset=1) == (1, 2)
    raises(TypeError, struct.unpack_from, format=">B", buffer=b"x")
    raises(TypeError, struct.pack_into, ">B", buffer=bytearray(1), offset=0)
    raises(TypeError, spec.pack_into, buffer=bytearray(1), offset=0)
    raises(TypeError, struct.iter_unpack, ">B", buffer=b"x")
    raises(TypeError, spec.iter_unpack, buffer=b"")
    assert raises(struct.error, struct.pack_into, ">z", None, None) == "bad char in struct format"
    assert raises(struct.error, struct.unpack_from, ">z", None) == "bad char in struct format"
    assert raises(struct.error, struct.iter_unpack, ">z", None) == "bad char in struct format"
    assert raises(struct.error, struct.iter_unpack, ">0H", b"") == (
        "cannot iteratively unpack with a struct of length 0")
    zero = bytearray(b"x")
    assert struct.pack_into("", zero, len(zero)) is None
    assert struct.unpack_from("", zero, len(zero)) == ()
    assert struct.unpack_from("", zero, -len(zero)) == ()
    raises(struct.error, struct.pack_into, "", zero, len(zero) + 1)
    raises(struct.error, struct.unpack_from, "", zero, -len(zero) - 1)
    assert zero == b"x"

    # CPython's opposite-endian 64-bit encoder can write the truncated
    # failing field. CloverVM leaves it zero after the shared encoder fails.
    if sys.byteorder == "little":
        foreign = ">"
        truncated = b"\x80\x00\x00\x00\x00\x00\x00\x00"
    else:
        foreign = "<"
        truncated = b"\x00\x00\x00\x00\x00\x00\x00\x80"
    buf = bytearray(b"abcdefghij")
    raises(struct.error, struct.pack_into, foreign + "Bq", buf, 1, 42, 1 << 63)
    if sys.implementation.name == "clovervm":
        assert buf == b"a*" + bytes(8)
    else:
        assert buf == b"a*" + truncated

    if sys.implementation.name == "clovervm":
        class Index:
            def __index__(self):
                raise AssertionError

        raises(TypeError, struct.pack_into, ">B", zero, Index(), 1)
        raises(TypeError, struct.unpack_from, ">B", zero, Index())
        raises(TypeError, struct.iter_unpack, ">B", zero)
        assert zero == b"x"

        spec = struct.Struct(">H")
        records = spec.iter_unpack(b"\x00\x01\x00\x02")
        assert next(records) == (1,)
        spec.__init__(">B")
        assert list(records) == [(2,)]
        assert list(spec.iter_unpack(b"\x00\x01")) == [(0,), (1,)]


check_buffer_operations()

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
