# Bounded bytearray semantics, checked against CPython 3.14.5.
# These are original regression cases; new-only construction and unsupported
# protocols are tested separately because they are intentional VM limits.
assert bytearray() == b""
assert bytearray(source=b"ab") == b"ab"
assert bytearray(3) == b"\x00\x00\x00"
assert bytearray(True) == b"\x00"
assert bytearray([0, 127, 255, True]) == b"\x00\x7f\xff\x01"
assert bytearray((65, 66)) == b"AB"
assert bytearray(range(3)) == b"\x00\x01\x02"
assert not bytearray()
assert bytearray(1)
assert len(bytearray(9)) == 9

source = bytearray(b"abc")
copy = bytearray(source)
frozen = bytes(source)
source[0] = 120
assert copy == b"abc" and frozen == b"abc" and source == b"xbc"
assert source[-1] == 99
source[-1] = False
assert source == b"xb\x00"
assert bytes(source[:2]) == b"xb"
assert source[::-1] == b"\x00bx"
assert source[100:] == b""
assert source[-100:100] == source
assert source[::100] == b"x"
huge = 10 ** 100
assert source[-huge:huge] == source
assert source[::huge] == b"x"
assert source[::-huge] == b"\x00"

source = bytearray(b"abcdef")
source[1:3] = b"XYZ"
assert source == b"aXYZdef"
source[4:2] = [33]
assert source == b"aXYZ!def"
source[1:4] = []
assert source == b"a!def"
source[::2] = b"ABC"
assert source == b"A!BeC"
source[::-2] = [120, 121, 122]
assert source == b"z!yex"
source[:] = source
assert source == b"z!yex"
source[1:3] = source
assert source == b"zz!yexex"

source = bytearray(b"abcdefg")
del source[1:6:2]
assert source == b"aceg"
del source[::-2]
assert source == b"ae"
del source[-1]
assert source == b"a"
del source[huge:]
assert source == b"a"
del source[:]
assert source == b""
source.append(65)
source.extend(b"BC")
source.extend(source)
assert source == b"ABCABC"
source.extend(range(3))
assert source == b"ABCABC\x00\x01\x02"
source.extend("")
source.clear()
source.extend(bytearray(b"xyz"))
assert source == b"xyz"

source = bytearray(b"ab")
it = iter(source)
assert iter(it) is it
assert next(it) == 97
source.append(99)
assert next(it) == 98
assert next(it) == 99
try:
    next(it)
except StopIteration:
    pass
else:
    raise AssertionError
source.extend(b"d")
try:
    next(it)
except StopIteration:
    pass
else:
    raise AssertionError
assert list(source) == [97, 98, 99, 100]

assert repr(bytearray()) == "bytearray(b'')"
assert repr(bytearray([39])) == "bytearray(b\"\\'\")"
assert repr(bytearray(b'"')) == 'bytearray(b\'"\')'
assert repr(bytearray([0, 9, 10, 13, 92, 255])) == "bytearray(b'\\x00\\t\\n\\r\\\\\\xff')"
assert str(bytearray(b"ab")) == "bytearray(b'ab')"
for i in range(30):
    a = bytearray(b"ab")
    assert a == bytearray(b"ab")
    assert a == b"ab" and b"ab" == a
    assert not (a != b"ab") and not (b"ab" != a)
    assert a != bytearray(b"ac") and a != b"a"
    assert b"a" != a and a != [97, 98]
    a[1] = 99
    assert a != b"ab" and b"ab" != a
    assert a == b"ac" and b"ac" == a
assert bytearray.__eq__(a, 1) is NotImplemented
assert bytearray.__ne__(a, 1) is NotImplemented
