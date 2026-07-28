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
assert b"clover".startswith(b"ov", start=2)
assert not b"clover".startswith(b"ov", start=3)
assert b.endswith(b"bc")
assert not b.endswith(b"ab")
assert b"clover".endswith(b"ov", end=4)
assert not b"clover".endswith(b"ov", end=3)
assert b"banana".find(b"na") == 2
assert b"banana".find(110) == 2
assert b"banana".find(122) == -1
assert b"banana".find(b"na", 3) == 4
assert b"banana".find(sub=b"na", start=3) == 4
assert b"banana".rfind(b"na") == 4
assert b"banana".rfind(110) == 4
assert b"banana".rfind(b"zz") == -1
assert b"banana".rfind(b"") == 6
assert b"banana".rfind(b"na", 0, 4) == 2
assert b"banana".rfind(sub=b"na", end=4) == 2
assert b"banana".index(b"na") == 2
assert b"banana".index(110) == 2
assert b"banana".index(sub=b"na", start=3) == 4
assert b"banana".rindex(b"na") == 4
assert b"banana".rindex(110) == 4
assert b"banana".count(b"na") == 2
assert b"banana".count(110) == 2
assert b"banana".count(b"") == 7
assert b"banana".count(b"na", 3) == 1
assert b"banana".count(sub=b"na", start=0, end=4) == 1
assert b"\x00\x01".find(True) == 1
assert b"\x00\x01".index(True) == 1
assert b"\x00\x01\x01".count(True) == 2

assert b"prefix-value".removeprefix(b"prefix-") == b"value"
assert b"prefix-value".removeprefix(b"missing") == b"prefix-value"
assert b"value.txt".removesuffix(b".txt") == b"value"
assert b"value.txt".removesuffix(b".py") == b"value.txt"
assert b"prefix-value".removeprefix(prefix=b"prefix-") == b"value"
assert b"value.txt".removesuffix(suffix=b".txt") == b"value"

assert b"banana".replace(b"na", b"NA") == b"baNANA"
assert b"banana".replace(b"na", b"NA", 1) == b"baNAna"
assert b"banana".replace(b"na", b"NA", 0) == b"banana"
assert b"banana".replace(b"na", b"NA", -2) == b"baNANA"
assert b"banana".replace(old=b"na", new=b"NA", count=1) == b"baNAna"
assert b"abc".replace(b"", b"-") == b"-a-b-c-"
assert b"abc".replace(b"", b"-", 0) == b"abc"
assert b"abc".replace(b"", b"-", 1) == b"-abc"
assert b"abc".replace(b"", b"-", 2) == b"-a-bc"
assert b"abc".replace(b"", b"-", 4) == b"-a-b-c-"
assert b"abc".replace(b"", b"-", 99) == b"-a-b-c-"

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
    b"abc".rindex(b"z")
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
