# Explicit CloverVM slice limits: custom __index__ is never invoked.
import binascii
calls = []
class Index:
    def __index__(self):
        calls.append('index')
        return 1
for encode in (binascii.hexlify, binascii.b2a_hex):
    try:
        encode(b'ab', b':', Index())
        assert False
    except TypeError:
        pass
assert calls == []

# Imported conversion operations survive rebinding of public builtins.
import builtins
saved = (builtins.bytes, builtins.bytearray, builtins.str, builtins.int,
         builtins.isinstance, builtins.len, builtins.ord, builtins.range)
raw = bytearray(b'ab')
try:
    builtins.bytes = None
    builtins.bytearray = None
    builtins.str = None
    builtins.int = None
    builtins.isinstance = None
    builtins.len = None
    builtins.ord = None
    builtins.range = None
    assert binascii.hexlify(raw, ':', 1) == b'61:62'
    assert binascii.unhexlify('61Ff') == b'a\xff'
finally:
    builtins.bytes = saved[0]
    builtins.bytearray = saved[1]
    builtins.str = saved[2]
    builtins.int = saved[3]
    builtins.isinstance = saved[4]
    builtins.len = saved[5]
    builtins.ord = saved[6]
    builtins.range = saved[7]

# Without a native buffer export, resizing in a separator callback is allowed.
# CPython raises BufferError here; this slice deliberately snapshots afterwards.
payload = bytearray(b'ab')
class ResizingSeparator(bytes):
    def __len__(self):
        payload.append(99)
        return 1
assert binascii.hexlify(payload, ResizingSeparator(b':')) == b'61:62:63'
assert payload == bytearray(b'abc')
