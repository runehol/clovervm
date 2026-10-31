# Independently written Base85 vectors, checked against CPython 3.14.10.
import base64

vectors = ((b'', b'', b''),
           (b'f', b'W&', b'W&i*H'),
           (b'fo', b'W^V', b'W^Vuh'),
           (b'foo', b'W^Zo', b'W^Zo*'),
           (b'foob', b'W^Zp|', b'W^Zp|'),
           (b'fooba', b'W^Zp|VE', b'W^Zp|VE_OC'),
           (b'foobar', b'W^Zp|VR8', b'W^Zp|VR8Tf'),
           (b'hello world', b'Xk~0{Zy<MXa%^M', b'Xk~0{Zy<MXa%^M('),
           (b'\x00', b'00', b'00000'),
           (b'\x00\x00\x00\x00', b'00000', b'00000'),
           (b'\xff\xff\xff\xff', b'|NsC0', b'|NsC0'))
for case in vectors:
    raw = case[0]
    encoded = case[1]
    padded = case[2]
    assert base64.b85encode(raw) == encoded
    assert base64.b85encode(raw, False) == encoded
    assert base64.b85encode(raw, pad=True) == padded
    assert base64.b85encode(bytearray(raw)) == encoded
    assert base64.b85decode(encoded) == raw
    assert base64.b85decode(bytearray(encoded)) == raw
    assert base64.b85decode(padded) == raw + b'\x00\x00\x00'[:(-len(raw)) % 4]
    assert base64.b85encode(raw).__class__ is bytes
    assert base64.b85decode(encoded).__class__ is bytes
assert base64.b85encode(b=b'abc', pad=True) == b'VPazd'
assert base64.b85decode(b='VPaz') == b'abc'
assert base64.b85decode('00000') == b'\x00\x00\x00\x00'
assert base64.b85decode(b'0') == b''
assert base64.b85decode(b'00') == b'\x00'
assert base64.b85decode(b'000') == b'\x00\x00'
assert base64.b85decode(b'0000') == b'\x00\x00\x00'
# A one-character tail is consumed and checked even though it emits no bytes.
assert base64.b85decode(b'00000!') == b'\x00\x00\x00\x00'
for name in ('b85encode', 'b85decode'):
    assert name in base64.__all__

# Repeated hot calls cover stable caches and all tail lengths.
for n in range(65):
    values = []
    for i in range(n):
        values.append((i * 73 + n) % 256)
    raw = bytes(values)
    for pad in (False, True):
        for repeat in range(3):
            encoded = base64.b85encode(raw, pad)
            decoded = base64.b85decode(encoded)
            assert decoded == raw + (b'\x00\x00\x00'[:(-n) % 4] if pad else b'')
