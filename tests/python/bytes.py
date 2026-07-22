b = b"abc"
assert bytes() == b""
assert bytes(b) is b
assert bytes([65, 66, 67]) == b"ABC"
assert bytes((0, 255)) == b"\x00\xff"

assert len(b) == 3
assert b[0] == 97
assert b[-1] == 99
assert b[1:] == b"bc"
assert b[::-1] == b"cba"

assert b"ab" + b"cd" == b"abcd"
assert b"a" < b"b"
assert b"abc" <= b"abc"
assert b"abd" > b"abc"
assert b"abd" >= b"abd"
assert b"abc" != b"abd"

assert b"b" in b
assert 98 in b
assert b"z" not in b
assert 122 not in b

assert b.startswith(b"ab")
assert not b.startswith(b"bc")
assert b.endswith(b"bc")
assert not b.endswith(b"ab")
assert b"banana".find(b"na") == 2
assert b"banana".find(110) == 2
assert b"banana".find(122) == -1
assert b"banana".index(b"na") == 2
assert b"banana".index(110) == 2
assert b"banana".count(b"na") == 2
assert b"banana".count(110) == 2
assert b"banana".count(b"") == 7
assert b"\x00\x01".find(True) == 1
assert b"\x00\x01".index(True) == 1
assert b"\x00\x01\x01".count(True) == 2

assert repr(b"abc") == "b'abc'"
assert str(b"abc") == "b'abc'"
assert repr(b"\x00\t\n\r\"'\\A\xff") == "b'\\x00\\t\\n\\r\"\\'\\\\A\\xff'"
assert br"raw\n" == b"raw\\n"
assert b"\x41\101" == b"AA"

assert hash(b"same") == hash(b"same")

try:
    bytes([256])
    assert False
except ValueError:
    pass

try:
    bytes([-1])
    assert False
except ValueError:
    pass

try:
    bytes([10 ** 100])
    assert False
except ValueError:
    pass

try:
    bytes((-10 ** 100,))
    assert False
except ValueError:
    pass

try:
    bytes([1.0])
    assert False
except TypeError:
    pass

try:
    bytes([True])
    assert False
except TypeError:
    pass

try:
    b[3]
    assert False
except IndexError:
    pass

try:
    b"abc" + "x"
    assert False
except TypeError:
    pass

try:
    b"abc".index(b"z")
    assert False
except ValueError:
    pass

try:
    b"abc".index(122)
    assert False
except ValueError:
    pass

try:
    b"abc".find(256)
    assert False
except ValueError:
    pass

try:
    b"abc".count(-1)
    assert False
except ValueError:
    pass
