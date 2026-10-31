# Independently written regressions checked against CPython 3.14.10.
import base64
import binascii


pairs = ((base64.b16encode, base64.b16decode),
         (base64.b32encode, base64.b32decode),
         (base64.b32hexencode, base64.b32hexdecode),
         (base64.b64encode, base64.b64decode),
         (base64.standard_b64encode, base64.standard_b64decode),
         (base64.urlsafe_b64encode, base64.urlsafe_b64decode))
for pair in pairs:
    encode = pair[0]
    decode = pair[1]
    for raw in (b'', b'f', b'fo', b'foo', b'foob', b'fooba', b'foobar',
                bytes(list(range(256)))):
        data = bytearray(raw)
        encoded = encode(data)
        assert encoded.__class__ is bytes
        assert encoded == encode(raw)
        assert bytes(data) == raw
        encoded_data = bytearray(encoded)
        decoded = decode(encoded_data)
        assert decoded.__class__ is bytes
        assert decoded == raw
        assert bytes(encoded_data) == encoded
        # Snapshots and returned results remain independent of later mutation.
        data.clear()
        encoded_data.clear()
        assert decoded == raw
        assert encoded == encode(raw)

for alt in (b'-_', b'/+', b'++', b'==', b'\x00\xff'):
    option = bytearray(alt)
    raw = bytearray(b'\xfb\xff')
    encoded = base64.b64encode(raw, option)
    assert encoded == base64.b64encode(bytes(raw), alt)
    for strict in (False, True):
        expected = base64.b64decode(encoded, alt, strict)
        assert base64.b64decode(bytearray(encoded), option, strict) == expected
    assert bytes(option) == alt

assert base64.b64decode(bytearray(b'-_8='), '-_') == b'\xfb\xff'
assert base64.b64decode('-_8=', bytearray(b'-_')) == b'\xfb\xff'
assert base64.b16decode(bytearray(b'666f6f'), True) == b'foo'
assert base64.b32decode(bytearray(b'mzxw6==='), True) == b'foo'
assert base64.b32hexdecode(bytearray(b'cpnmu==='), True) == b'foo'
assert base64.b32decode('01======', map01=bytearray(b'L')) == b'r'
assert base64.b32decode(bytearray(b'01======'), map01='L') == b'r'
for mapping in (b'I', b'L', b'X', b'\x00', b'\xff'):
    for data in (b'', b'M1O23456', b'01======'):
        try:
            expected = base64.b32decode(data, map01=mapping)
        except binascii.Error:
            caught = False
            try:
                base64.b32decode(bytearray(data), map01=bytearray(mapping))
            except binascii.Error:
                caught = True
            assert caught
        else:
            assert base64.b32decode(bytearray(data), map01=bytearray(mapping)) == expected


def raises(call, args, kind):
    caught = False
    try:
        call(*args)
    except kind:
        caught = True
    assert caught


for alt in (bytearray(), bytearray(b'x'), bytearray(b'xyz')):
    raises(base64.b64encode, (bytearray(), alt), AssertionError)
    raises(base64.b64decode, (bytearray(), alt), AssertionError)
for mapping in (bytearray(), bytearray(b'xx')):
    raises(base64.b32decode, (bytearray(), False, mapping), AssertionError)
# Invalid data wins over invalid options at each wrapper's specified stage.
raises(base64.b64encode, (None, bytearray(b'x')), TypeError)
raises(base64.b64decode, (chr(233), bytearray(b'x')), ValueError)
raises(base64.b32decode, (bytearray(b'x'), False, []), binascii.Error)
raises(base64.b64encode, (bytearray(), '-_'), TypeError)
raises(base64.b64decode, (bytearray(), chr(233)), ValueError)
for pair in pairs:
    encode = pair[0]
    decode = pair[1]
    for invalid in (None, 42, [], [65, 65], (65, 65)):
        raises(encode, (invalid,), TypeError)
        raises(decode, (invalid,), TypeError)
for case in ((base64.b16decode, b'0'),
             (base64.b16decode, b'gg'),
             (base64.b32decode, b'MY====='),
             (base64.b32decode, b'!Y======'),
             (base64.b32hexdecode, b'CO====='),
             (base64.b64decode, b'Zg=')):
    raises(case[0], (bytearray(case[1]),), binascii.Error)
raises(base64.b64decode, (bytearray(b'Z!g=='), None, True), binascii.Error)
assert base64.b64decode(bytearray(b'Z!g==')) == b'f'
# Data and an option may be the same mutable object.
shared = bytearray(b'-_')
assert base64.b64encode(shared, shared) == b'LV8='
raises(base64.b64decode, (shared, shared), binascii.Error)
