# Python test set -- bounded base64 module
# Portions adapted from CPython Lib/test/test_base64.py and
# Lib/test/test_binascii.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Additional independently written cases checked against CPython 3.14.5.

import base64
import binascii

assert base64.binascii is binascii
assert issubclass(binascii.Error, ValueError)
assert binascii.Error.__bases__ == (ValueError,)
assert binascii.Error.__module__ == "binascii"
assert binascii.Error("bad").args == ("bad",)
assert base64.standard_b64encode is not base64.b64encode
assert base64.standard_b64decode is not base64.b64decode

vectors = [(b"", b""), (b"f", b"Zg=="), (b"fo", b"Zm8="),
           (b"foo", b"Zm9v"), (b"foob", b"Zm9vYg=="),
           (b"fooba", b"Zm9vYmE="), (b"foobar", b"Zm9vYmFy"),
           (b"www.python.org", b"d3d3LnB5dGhvbi5vcmc="),
           (b"\x00", b"AA=="), (b"\xfb\xff", b"+/8=")]
for vector in vectors:
    raw = vector[0]
    encoded = vector[1]
    assert base64.b64encode(raw) == encoded
    assert base64.standard_b64encode(raw) == encoded
    assert base64.b64decode(encoded) == raw
    assert base64.b64decode(encoded, validate=True) == raw
    assert base64.standard_b64decode(encoded) == raw

values = []
for i in range(256):
    values.append(i)
    raw = bytes([i])
    assert base64.b64decode(base64.b64encode(raw), validate=True) == raw
raw = bytes(values)
all_bytes_encoded = (
    b"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4v" +
    b"MDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5f" +
    b"YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6P" +
    b"kJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/" +
    b"wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v" +
    b"8PHy8/T19vf4+fr7/P3+/w=="
)
assert base64.b64encode(raw) == all_bytes_encoded
assert base64.b64decode(all_bytes_encoded, validate=True) == raw
for size in range(257):
    part = raw[:size]
    assert base64.b64decode(base64.b64encode(part), validate=True) == part
    assert base64.urlsafe_b64decode(base64.urlsafe_b64encode(part)) == part

assert base64.b64encode(s=b"\xfb\xff", altchars=b"-_") == b"-_8="
assert base64.b64decode(s="-_8=", altchars="-_", validate=True) == b"\xfb\xff"
assert base64.b64decode("Zg==") == b"f"
assert base64.standard_b64decode(s="Zm8=") == b"fo"
assert base64.standard_b64encode(s=b"fo") == b"Zm8="
assert base64.urlsafe_b64encode(s=b"\xfb\xff") == b"-_8="
assert base64.urlsafe_b64decode(s="-_8=") == b"\xfb\xff"
assert base64.urlsafe_b64decode(b"+/8=") == b"\xfb\xff"
assert base64.b64decode(b"++//", b"-_", True) == b"\xfb\xef\xff"
assert base64.b64decode(b"--__", b"-_", True) == b"\xfb\xef\xff"
assert base64.b64encode(b"\xfb\xff", b"aa") == b"aa8="
assert base64.b64decode(b"aa==", b"aa") == b"\xff"
assert base64.b64encode(b"\xfb\xff", b"A=") == b"A=8="
assert base64.b64decode(b"aa==", b"A=") == b"i\xaf\xff"
assert base64.b64decode(b"aa==", b"=A") == b"i\xaf\xbe"
assert base64.b64encode(b"\xfb\xff", b"\xff\xfe") == b"\xff\xfe8="
assert base64.b64decode(b"\xff\xfe8=", b"\xff\xfe", True) == b"\xfb\xff"
for alt in (b"*$", b"+/", b"/+", b"+_", b"-+", b"-/", b"/_"):
    encoded = base64.b64encode(b"\xd3V\xbeo\xf7\x1d", alt)
    assert base64.b64decode(encoded, alt, True) == b"\xd3V\xbeo\xf7\x1d"


class ByteSubclass(bytes):
    def __bytes__(self):
        raise AssertionError("must use stored bytes")

    def __len__(self):
        raise AssertionError("must use stored bytes")

    def __getitem__(self, key):
        raise AssertionError("must use stored bytes")


class StringSubclass(str):
    def __len__(self):
        raise AssertionError("must use stored text")

    def __getitem__(self, key):
        raise AssertionError("must use stored text")


assert base64.b64encode(ByteSubclass(b"foo")) == b"Zm9v"
assert base64.b64decode(ByteSubclass(b"Zm9v")) == b"foo"
assert base64.b64decode(StringSubclass("Zm9v")) == b"foo"
assert base64.b64encode(ByteSubclass(b"foo")).__class__ is bytes
assert base64.b64decode(ByteSubclass(b"Zm9v")).__class__ is bytes
assert base64.b64decode(" Z\x00g==\t\n") == b"f"

permissive = [(b"=", b""), (b"====", b""), (b"!!!", b""),
              (b"AA=\n=", b"\x00"), (b"AA\xff==", b"\x00"),
              (b"AA==\xff", b"\x00"), (b"AAAA=", b"\x00\x00\x00"),
              (b"AAAA====", b"\x00\x00\x00"), (b"AA===", b"\x00"),
              (b"A=AAA", b"\x00\x00\x00"), (b"ab=c=", b"i\xb7")]
for vector in permissive:
    encoded = vector[0]
    decoded = vector[1]
    for decoder in (base64.b64decode, base64.standard_b64decode,
                    base64.urlsafe_b64decode):
        assert decoder(encoded) == decoded
for strict in (False, True):
    # Strict mode does not enforce zero unused bits.
    assert base64.b64decode(b"AB==", validate=strict) == b"\x00"
    assert base64.b64decode(b"AAB=", validate=strict) == b"\x00\x00"


def decode_error(data, strict, message):
    try:
        base64.b64decode(data, validate=strict)
        assert False
    except binascii.Error as error:
        assert error.__class__ is binascii.Error
        assert isinstance(error, ValueError)
        assert str(error) == message


for data in (b"AA", b"AAA", b"AA=A", b"AA==A", b"AAA=AAAA", b"AA===A"):
    decode_error(data, False, "Incorrect padding")
for data in (b"AA", b"AAA"):
    decode_error(data, True, "Incorrect padding")
for data in (b"=", b"====", b"=AAA"):
    decode_error(data, True, "Leading padding not allowed")
for data in (b"AAAA=", b"AAAA====", b"AA===", b"AA===A"):
    decode_error(data, True, "Excess padding not allowed")
for data in (b"AA=\n=", b"AA\xff==", b"AA==\xff", b"!!!"):
    decode_error(data, True, "Only base64 data is allowed")
decode_error(b"AA=A", True, "Discontinuous padding not allowed")
for data in (b"AA==A", b"AAA=AAAA"):
    decode_error(data, True, "Excess data after padding")
for data in (b"A", b"A=AAA"):
    decode_error(data, True, "Invalid base64-encoded string: number of data characters (1) cannot be 1 more than a multiple of 4")
decode_error(b"AAAAA", False, "Invalid base64-encoded string: number of data characters (5) cannot be 1 more than a multiple of 4")

# validate uses truth testing, rather than requiring an exact bool.
for flag in (None, 0, [], ""):
    assert base64.b64decode(b"!", validate=flag) == b""
for flag in (1, [1], "x", 1.0):
    decode_error(b"!", flag, "Only base64 data is allowed")


def raises(call, args, expected):
    caught = False
    try:
        call(*args)
    except expected:
        caught = True
    assert caught


def unknown_keyword(call):
    return call(unknown=b"foo")


for value in (None, 42, [], [90, 103, 61, 61], (90, 103, 61, 61)):
    raises(base64.b64encode, (value,), TypeError)
    raises(base64.b64decode, (value,), TypeError)
for encoder in (base64.b64encode, base64.standard_b64encode, base64.urlsafe_b64encode):
    raises(encoder, ("foo",), TypeError)
    raises(encoder, (), TypeError)
    raises(unknown_keyword, (encoder,), TypeError)
for decoder in (base64.b64decode, base64.standard_b64decode, base64.urlsafe_b64decode):
    raises(decoder, ("Zg==" + chr(233),), ValueError)
    raises(decoder, (), TypeError)
    raises(unknown_keyword, (decoder,), TypeError)
for alt in (b"", b"x", b"xyz"):
    raises(base64.b64encode, (b"", alt), AssertionError)
    raises(base64.b64decode, (b"", alt), AssertionError)
raises(base64.b64encode, (b"", "-_"), TypeError)
raises(base64.b64decode, (b"", chr(233) + "_"), ValueError)
raises(base64.b64decode, (b"", [45, 95]), TypeError)
raises(base64.b64encode, (b"", [45, 95]), TypeError)
raises(base64.b64encode, (b"", []), AssertionError)
raises(base64.b64decode, (b"", []), TypeError)
raises(base64.b64encode, (None, b"x"), TypeError)
raises(base64.b64decode, (chr(233), b"x"), ValueError)
raises(base64.b64encode, (b"", None, False), TypeError)
raises(base64.b64decode, (b"", None, False, False), TypeError)
raises(base64.standard_b64decode, (b"", True), TypeError)
raises(base64.standard_b64encode, (b"", b"-_"), TypeError)
raises(base64.urlsafe_b64decode, (b"", True), TypeError)
raises(base64.urlsafe_b64encode, (b"", b"-_"), TypeError)
