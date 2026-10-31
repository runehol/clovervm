# Independent Ascii85 vectors and assertions, checked against CPython 3.14.10.
import base64

vectors = ((b'', b''), (b'f', b'Ac'), (b'fo', b'Ao@'),
           (b'foo', b'AoDS'), (b'foob', b'AoDTs'),
           (b'foobar', b'AoDTs@<)'), (b'\x00', b'!!'),
           (b'\x00\x00', b'!!!'), (b'\x00\x00\x00', b'!!!!'),
           (b'\x00\x00\x00\x00', b'z'),
           (b'    ', b'+<VdL'), (b'\xff\xff\xff\xff', b's8W-!'))
for case in vectors:
    raw = case[0]
    encoded = case[1]
    assert base64.a85encode(raw) == encoded
    assert base64.a85encode(bytearray(raw)) == encoded
    assert base64.a85decode(encoded) == raw
    assert base64.a85decode(bytearray(encoded)) == raw
    assert base64.a85encode(raw, adobe=True) == b'<~' + encoded + b'~>'
    assert base64.a85decode(encoded + b'~>', adobe=True) == raw
    assert base64.a85decode(b'<~' + encoded + b'~>', adobe=True) == raw
    assert base64.a85encode(raw).__class__ is bytes
    assert base64.a85decode(encoded).__class__ is bytes
assert base64.a85decode(b='AoDS') == b'foo'
assert base64.a85encode(b=b'foo') == b'AoDS'
assert base64.a85decode(b'!') == b''
assert base64.a85decode('z') == b'\x00\x00\x00\x00'
assert base64.a85encode(b'     ', foldspaces=True) == b'y+9'
assert base64.a85decode(b'y+9', foldspaces=True) == b'     '
assert base64.a85decode(b' \t\nz\r\v') == b'\x00\x00\x00\x00'
# Digits and z take precedence over ignorechars; y is ignored only if disabled.
assert base64.a85decode(b'!!!!!', ignorechars=b'!') == b'\x00\x00\x00\x00'
assert base64.a85decode(b'z', ignorechars=b'z') == b'\x00\x00\x00\x00'
assert base64.a85decode(b'y', ignorechars=b'y') == b''
assert base64.a85decode(b'y', foldspaces=True, ignorechars=b'y') == b'    '
assert base64.a85decode(b'!!!!!', ignorechars=None) == b'\x00\x00\x00\x00'
assert base64.a85decode(b' ', ignorechars=[32]) == b''
for n in range(41):
    values = []
    for i in range(n):
        values.append((i * 73 + n) % 256)
    raw = bytes(values)
    for pad in (False, True):
        for adobe in (False, True):
            for foldspaces in (False, True):
                encoded = base64.a85encode(raw, pad=pad, adobe=adobe,
                                           foldspaces=foldspaces, wrapcol=7)
                assert base64.a85decode(encoded, adobe=adobe, foldspaces=foldspaces) == raw + (b'\0\0\0'[:(-n) % 4] if pad else b'')
for wrap in (-1, 1, 2, 3):
    assert base64.a85encode(b'', adobe=True, wrapcol=wrap) == b'<~\n~>'
assert base64.a85encode(b'', adobe=True, wrapcol=4) == b'<~~>'
assert base64.a85encode(b'foob', wrapcol=2) == b'Ao\nDT\ns'
assert base64.a85encode(b'foob', wrapcol=3, adobe=True) == b'<~A\noDT\ns~>'
for name in ('a85encode', 'a85decode'):
    assert name in base64.__all__

# Width validation and clipping must not inherit native range/slice SMI bounds.
for width in (1152921504606846975, 1152921504606846976,
              9223372036854775808, 18446744073709551616, 10 ** 100):
    assert base64.a85encode(b'foo', wrapcol=width) == b'AoDS'
    assert base64.a85encode(b'', wrapcol=width) == b''
    assert base64.a85encode(b'foo', wrapcol=width, adobe=True) == b'<~AoDS~>'
    assert base64.a85encode(b'foo', wrapcol=-width) == b'A\no\nD\nS'
