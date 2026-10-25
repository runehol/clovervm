# Independently written Base16/Base32 regressions, checked with CPython 3.14.5.
import base64
import binascii


encoders = (base64.b16encode, base64.b32encode, base64.b32hexencode)
decoders = (base64.b16decode, base64.b32decode, base64.b32hexdecode)
for encode in encoders:
    for invalid in ("abc", 1, None, [1, 2], (1, 2)):
        caught = False
        try:
            encode(invalid)
        except TypeError:
            caught = True
        assert caught
for decode in decoders:
    for invalid in (1, None, [65, 65], (65, 65)):
        caught = False
        try:
            decode(invalid)
        except TypeError:
            caught = True
        assert caught
    caught = False
    try:
        decode(chr(233))
    except ValueError as error:
        caught = True
        assert error.__class__ is ValueError
        assert str(error) == "string argument should contain only ASCII characters"
    assert caught
for call in encoders + decoders:
    caught = False
    try:
        call()
    except TypeError:
        caught = True
    assert caught
    caught = False
    try:
        call(b"", unknown=True)
    except TypeError:
        caught = True
    assert caught
for call in encoders:
    caught = False
    try:
        call(b"", True)
    except TypeError:
        caught = True
    assert caught
for call in (base64.b16decode, base64.b32hexdecode):
    caught = False
    try:
        call(b"", False, None)
    except TypeError:
        caught = True
    assert caught
caught = False
try:
    base64.b32hexdecode(b"", map01="L")
except TypeError:
    caught = True
assert caught
for name in ("b16encode", "b16decode", "b32encode", "b32decode",
             "b32hexencode", "b32hexdecode"):
    assert name in base64.__all__
