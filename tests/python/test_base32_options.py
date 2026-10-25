# Independently written Base16/Base32 regressions, checked with CPython 3.14.5.
import base64
import binascii


assert base64.b32decode(b"M1023456", map01=b"L") == b"b\xdd\xad\xf3\xbe"
assert base64.b32decode("M1023456", False, "I") == b"b\x1d\xad\xf3\xbe"
assert base64.b32decode(b"01======", True, "l") == b"r"
assert base64.b32decode(b"11======", map01="A") == b"\x00"
assert base64.b32decode(b"11======", map01="2") == b"\xd6"
assert base64.b32decode(b"10======", map01="O") == b"s"
assert base64.b32decode(b"MY======", map01=b"\xff") == b"f"
# Mapping is simultaneous, and may introduce or remove trailing padding.
assert base64.b32decode(b"AA111111", map01="=") == b"\x00"
assert base64.b32decode(b"11111111", map01="A") == b"\x00\x00\x00\x00\x00"


def check_error(s, casefold, map01, kind, message):
    caught = False
    try:
        base64.b32decode(s, casefold, map01)
    except Exception as error:
        caught = True
        assert error.__class__ is kind
        assert str(error) == message
    assert caught


check_error(b"", False, b"", AssertionError, "b''")
check_error(b"", False, b"IL", AssertionError, "b'IL'")
check_error(b"", False, 3, TypeError,
            "argument should be a bytes-like object or ASCII string, not 'int'")
check_error(b"", False, chr(233), ValueError,
            "string argument should contain only ASCII characters")
check_error(b"A", False, 3, binascii.Error, "Incorrect padding")
check_error(b"!!!!!!!!", False, 3, TypeError,
            "argument should be a bytes-like object or ASCII string, not 'int'")
check_error(b"01======", False, "l", binascii.Error, "Non-base32 digit found")
check_error(b"11======", False, "=", binascii.Error, "Incorrect padding")
check_error(b"10======", False, "0", binascii.Error, "Non-base32 digit found")


class Flag:
    def __bool__(self):
        self.calls += 1
        return True


flag = Flag()
for case in ((base64.b16decode, b"ff"),
                     (base64.b32decode, b"me======"),
                     (base64.b32hexdecode, b"c4======")):
    decode = case[0]
    data = case[1]
    flag.calls = 0
    assert decode(data, flag)
    assert flag.calls == 1
    flag.calls = 0
    assert decode(b"", flag) == b""
    assert flag.calls == 1
flag.calls = 0
check_error(b"A", flag, None, binascii.Error, "Incorrect padding")
check_error(b"AAAAAAAA", flag, b"", AssertionError, "b''")
assert flag.calls == 0


failure = RuntimeError("casefold failed")


class RaisingFlag:
    def __bool__(self):
        raise failure


for decode in (base64.b16decode, base64.b32decode, base64.b32hexdecode):
    caught = False
    try:
        decode(b"", RaisingFlag())
    except RuntimeError as error:
        caught = True
        assert error is failure
    assert caught
    # Non-ASCII conversion happens before casefold truth testing.
    caught = False
    try:
        decode(chr(233), RaisingFlag())
    except ValueError as error:
        caught = True
        assert error.__class__ is ValueError
    assert caught
check_error(b"A", RaisingFlag(), None, binascii.Error, "Incorrect padding")
check_error(b"", RaisingFlag(), b"", AssertionError, "b''")
