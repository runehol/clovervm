# Python test set -- bounded Base16/Base32 conversion
# Portions adapted from CPython Lib/test/test_base64.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
import base64
import binascii


vectors = [(b"", b""), (b"\x00", b"00"),
           (b"\x01\x02\xab\xcd\xef", b"0102ABCDEF"),
           (b"foobar", b"666F6F626172"), (b"\xff\x80\x7f", b"FF807F")]
for vector in vectors:
    raw = vector[0]
    encoded = vector[1]
    assert base64.b16encode(raw) == encoded
    assert base64.b16decode(encoded) == raw
    assert base64.b16encode(raw).__class__ is bytes
    assert base64.b16decode(encoded).__class__ is bytes
assert base64.b16decode("0102ABCDEF") == b"\x01\x02\xab\xcd\xef"
assert base64.b16decode(s="aBcD", casefold=True) == b"\xab\xcd"
assert base64.b16decode(b"ff", 1) == b"\xff"
assert base64.b16encode(s=b"foo") == b"666F6F"
values = []
for i in range(256):
    values.append(i)
raw = bytes(values)
assert base64.b16decode(base64.b16encode(raw)) == raw
assert len(base64.b16encode(raw)) == 512


def check_error(s, message):
    caught = False
    try:
        base64.b16decode(s)
    except binascii.Error as error:
        caught = True
        assert str(error) == message
        assert error.__class__ is binascii.Error
    assert caught


for bad in (b"ff", b"0x", b"0 ", b"00\n", b"=", b"\xff", b"0\x00"):
    check_error(bad, "Non-base16 digit found")
for odd in (b"0", b"ABC", "01234"):
    check_error(odd, "Odd-length string")
# Alphabet validation precedes the odd-length check.
check_error(b"X", "Non-base16 digit found")
check_error(b"00X", "Non-base16 digit found")
