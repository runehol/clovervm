# Explicit CloverVM CRC slice limits: custom index seeds are rejected.
# Independent tests. CPython accepts these seeds and exports the input buffer
# before __index__, permitting content mutation but preventing resizing.

import binascii

payload = bytearray(b"a")
calls = []


class MutatingIndex:
    def __index__(self):
        calls.append("mutate")
        payload[0] = 98
        return 0


class ResizingIndex:
    def __index__(self):
        calls.append("resize")
        payload.append(98)
        return 0


class RaisingIndex:
    def __index__(self):
        calls.append("raise")
        raise LookupError("seed")


for function in (binascii.crc32, binascii.crc_hqx):
    for seed in (MutatingIndex(), ResizingIndex(), RaisingIndex()):
        for data in (payload, b"", "invalid payload"):
            try:
                function(data, seed)
                assert False, "custom index seed must be rejected"
            except TypeError:
                pass
assert calls == []
assert payload == bytearray(b"a")

# Invalid data is diagnosed before an invalid seed.
try:
    binascii.crc32("invalid payload", None)
    assert False
except TypeError as error:
    assert error.args == ("a bytes-like object is required",)

# Rebinding public builtins after import must not affect native CRC calls.
import builtins

saved_bytes = builtins.bytes
saved_bytearray = builtins.bytearray
saved_int = builtins.int
saved_isinstance = builtins.isinstance
saved_range = builtins.range
saved_type_error = builtins.TypeError
raw = bytearray(b"123456789")
try:
    builtins.bytes = None
    builtins.bytearray = None
    builtins.int = None
    builtins.isinstance = None
    builtins.range = None
    builtins.TypeError = None
    assert binascii.crc32(raw) == 3421780262
    assert binascii.crc_hqx(raw, -1) == 10673
    assert binascii.crc32(b"123456789") == 3421780262
    assert binascii.crc_hqx(b"123456789", 0) == 12739
    try:
        binascii.crc32(None)
        assert False
    except saved_type_error:
        pass
finally:
    builtins.bytes = saved_bytes
    builtins.bytearray = saved_bytearray
    builtins.int = saved_int
    builtins.isinstance = saved_isinstance
    builtins.range = saved_range
    builtins.TypeError = saved_type_error
