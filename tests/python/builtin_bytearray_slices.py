# Original fixed slice oracles generated with CPython 3.14.5.
a = bytearray(range(8))
assert a[slice(None, None, 1)] == b'\x00\x01\x02\x03\x04\x05\x06\x07'
del a[slice(None, None, 1)]
assert a == b''
a = bytearray(range(8))
a[slice(None, None, 1)] = b''
assert a == b''
a = bytearray(range(8))
a[slice(None, None, 1)] = b'de'
assert a == b'de'
a = bytearray(range(8))
a[slice(None, None, 1)] = b'defghijklm'
assert a == b'defghijklm'
a = bytearray(range(8))
assert a[slice(None, None, -1)] == b'\x07\x06\x05\x04\x03\x02\x01\x00'
del a[slice(None, None, -1)]
assert a == b''
a = bytearray(range(8))
a[slice(None, None, -1)] = b'defghijk'
assert a == b'kjihgfed'
a = bytearray(range(8))
assert a[slice(-20, 20, 2)] == b'\x00\x02\x04\x06'
del a[slice(-20, 20, 2)]
assert a == b'\x01\x03\x05\x07'
a = bytearray(range(8))
a[slice(-20, 20, 2)] = b'defg'
assert a == b'd\x01e\x03f\x05g\x07'
a = bytearray(range(8))
assert a[slice(6, 1, -2)] == b'\x06\x04\x02'
del a[slice(6, 1, -2)]
assert a == b'\x00\x01\x03\x05\x07'
a = bytearray(range(8))
a[slice(6, 1, -2)] = b'def'
assert a == b'\x00\x01f\x03e\x05d\x07'
a = bytearray(range(8))
assert a[slice(4, 2, 1)] == b''
del a[slice(4, 2, 1)]
assert a == b'\x00\x01\x02\x03\x04\x05\x06\x07'
a = bytearray(range(8))
a[slice(4, 2, 1)] = b''
assert a == b'\x00\x01\x02\x03\x04\x05\x06\x07'
a = bytearray(range(8))
a[slice(4, 2, 1)] = b'de'
assert a == b'\x00\x01\x02\x03de\x04\x05\x06\x07'
a = bytearray(range(8))
a[slice(4, 2, 1)] = b'defghijklm'
assert a == b'\x00\x01\x02\x03defghijklm\x04\x05\x06\x07'
a = bytearray(range(8))
assert a[slice(None, -100, -1)] == b'\x07\x06\x05\x04\x03\x02\x01\x00'
del a[slice(None, -100, -1)]
assert a == b''
a = bytearray(range(8))
a[slice(None, -100, -1)] = b'defghijk'
assert a == b'kjihgfed'
a = bytearray(range(8))
assert a[slice(100, 100, 1)] == b''
del a[slice(100, 100, 1)]
assert a == b'\x00\x01\x02\x03\x04\x05\x06\x07'
a = bytearray(range(8))
a[slice(100, 100, 1)] = b''
assert a == b'\x00\x01\x02\x03\x04\x05\x06\x07'
a = bytearray(range(8))
a[slice(100, 100, 1)] = b'de'
assert a == b'\x00\x01\x02\x03\x04\x05\x06\x07de'
a = bytearray(range(8))
a[slice(100, 100, 1)] = b'defghijklm'
assert a == b'\x00\x01\x02\x03\x04\x05\x06\x07defghijklm'
a = bytearray(range(8))
assert a[slice(-100, None, 3)] == b'\x00\x03\x06'
del a[slice(-100, None, 3)]
assert a == b'\x01\x02\x04\x05\x07'
a = bytearray(range(8))
a[slice(-100, None, 3)] = b'def'
assert a == b'd\x01\x02e\x04\x05f\x07'
a = bytearray(range(8))
assert a[slice(None, None, 10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000)] == b'\x00'
del a[slice(None, None, 10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000)]
assert a == b'\x01\x02\x03\x04\x05\x06\x07'
a = bytearray(range(8))
a[slice(None, None, 10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000)] = b'd'
assert a == b'd\x01\x02\x03\x04\x05\x06\x07'
a = bytearray(range(8))
assert a[slice(None, None, -10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000)] == b'\x07'
del a[slice(None, None, -10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000)]
assert a == b'\x00\x01\x02\x03\x04\x05\x06'
a = bytearray(range(8))
a[slice(None, None, -10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000)] = b'd'
assert a == b'\x00\x01\x02\x03\x04\x05\x06d'


def check_empty_extended_slice_assignment():
    for case in [(2, b"bdf"), (-2, b"ace"),
                 (3, b"bcef"), (-3, b"abde"),
                 (10 ** 100, b"bcdef"), (-10 ** 100, b"abcde")]:
        step = case[0]
        expected = case[1]
        for empty in [b"", bytearray(), [], (), range(0)]:
            a = bytearray(b"abcdef")
            a[::step] = empty
            assert a == expected
    a = bytearray(b"abc")
    a[1:1:2] = b""
    assert a == b"abc"
    a[1:3:2] = b""
    assert a == b"ac"


check_empty_extended_slice_assignment()
