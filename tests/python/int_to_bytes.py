# Original regressions checked against CPython 3.14.10.

def raises(error_type, call, *args, **kwargs):
    try:
        call(*args, **kwargs)
    except error_type as error:
        return str(error)
    raise AssertionError("expected conversion to fail")


assert (65).to_bytes() == b"A"
assert True.to_bytes() == b"\x01"
assert int.to_bytes(False, length=True) == b"\x00"
assert int.to_bytes(0, length=False, byteorder="little", signed=True) == b""
assert (256).to_bytes(byteorder="little", length=2) == b"\x00\x01"
assert (256).to_bytes(2, "big") == b"\x01\x00"
assert (0).to_bytes(0).__class__ is bytes

for case in (
    (255, 1, False, b"\xff", b"\xff"),
    (127, 1, True, b"\x7f", b"\x7f"),
    (128, 2, True, b"\x00\x80", b"\x80\x00"),
    (-1, 3, True, b"\xff\xff\xff", b"\xff\xff\xff"),
    (-128, 1, True, b"\x80", b"\x80"),
    (-129, 2, True, b"\xff\x7f", b"\x7f\xff"),
    ((1 << 32) + 1, 6, False, b"\x00\x01\x00\x00\x00\x01",
     b"\x01\x00\x00\x00\x01\x00"),
    (-(1 << 32), 6, True, b"\xff\xff\x00\x00\x00\x00",
     b"\x00\x00\x00\x00\xff\xff"),
    (-(1 << 63), 8, True, b"\x80\x00\x00\x00\x00\x00\x00\x00",
     b"\x00\x00\x00\x00\x00\x00\x00\x80"),
    ((1 << 64) - 1, 8, False, b"\xff\xff\xff\xff\xff\xff\xff\xff",
     b"\xff\xff\xff\xff\xff\xff\xff\xff"),
):
    number = case[0]
    length = case[1]
    signed = case[2]
    assert int.to_bytes(number, length, "big", signed=signed) == case[3]
    assert number.to_bytes(length, "little", signed=signed) == case[4]

for case in (
    (-1, 0, False), (-1, 0, True), (1, 0, False), (1, 0, True),
    (-1, 2, False), (256, 1, False), (128, 1, True), (-129, 1, True),
    (1 << 64, 8, False), (1 << 63, 8, True), (-(1 << 63) - 1, 8, True),
    (-(1 << 63) - (1 << 32), 8, True),
):
    for byteorder in ("big", "little"):
        raises(OverflowError, case[0].to_bytes, case[1], byteorder,
               signed=case[2])

raises(TypeError, int.to_bytes, self=0)
raises(TypeError, int.to_bytes, 1, 1, "big", False)
raises(TypeError, int.to_bytes, 1, 1, length=1)
raises(TypeError, int.to_bytes, 1, 1, "big", byteorder="big")
raises(TypeError, int.to_bytes, 1, unknown=True)


class StoredInt(int):
    def __index__(self):
        raise AssertionError("unexpected __index__")

    def __int__(self):
        raise AssertionError("unexpected __int__")

    def bit_length(self):
        raise AssertionError("unexpected bit_length")


class StoredOrder(str):
    def __eq__(self, other):
        raise AssertionError("unexpected __eq__")


log = []


class Signed:
    def __bool__(self):
        log.append("truth")
        assert (4660).to_bytes(2) == b"\x12\x34"
        return True


class Empty:
    def __len__(self):
        log.append("length")
        return 0


class Raising:
    def __bool__(self):
        log.append("raises")
        raise RuntimeError("signed callback")


receiver = StoredInt((1 << 64) + 1)
bound = receiver.to_bytes
assert bound(StoredInt(9), StoredOrder("big"), signed=Signed()) == (
    b"\x01\x00\x00\x00\x00\x00\x00\x00\x01"
)
assert (255).to_bytes(signed=Empty()) == b"\xff"
assert StoredInt(-128).to_bytes(signed=Signed()) == b"\x80"
assert log == ["truth", "length", "truth"]

raises(TypeError, int.to_bytes, "receiver", signed=Raising())
raises(TypeError, (0).to_bytes, 1.0, None, signed=Raising())
raises(OverflowError, (0).to_bytes, 1 << 63, None, signed=Raising())
raises(OverflowError, (0).to_bytes, -(1 << 63) - 1, None, signed=Raising())
raises(TypeError, (0).to_bytes, 1, None, signed=Raising())
assert log == ["truth", "length", "truth"]
assert raises(RuntimeError, (0).to_bytes, -1, "bad", signed=Raising()) == (
    "signed callback"
)
assert log == ["truth", "length", "truth", "raises"]
assert raises(ValueError, (0).to_bytes, -1, "bad") == (
    "byteorder must be either 'little' or 'big'"
)
assert raises(ValueError, (0).to_bytes, -(1 << 63), "big") == (
    "length argument must be non-negative"
)
raises(ValueError, (0).to_bytes, 1, StoredOrder("big\x00"))

large = (-129).to_bytes(65537, "little", signed=True)
assert len(large) == 65537
assert large[0] == 127 and large[1] == 255 and large[-1] == 255
