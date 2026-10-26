# Stored-byte ordering agrees with immutable bytes, including unsigned values
# and prefix lengths. Repeat each shape pair to exercise warmed operator caches.
def compare(left, right):
    return (left == right, left != right, left < right, left <= right,
            left > right, left >= right)


def check_comparisons():
    cases = [b"", b"a", b"ab", b"b", b"\x00", b"\x7f", b"\x80", b"\xff",
             b"a\x00", b"a\xff"]
    for left in cases:
        for right in cases:
            expected = compare(left, right)
            a = bytearray(left)
            b = bytearray(right)
            for repeat in range(3):
                assert compare(a, b) == expected
            for repeat in range(3):
                assert compare(a, right) == expected
            for repeat in range(3):
                assert compare(left, b) == expected
    a = bytearray(b"a")
    b = bytearray(b"b")
    assert compare(a, b) == (False, True, True, True, False, False)
    a[:] = b"z"
    assert compare(a, b) == (False, True, False, False, True, True)
    b[:] = b"z"
    assert compare(a, b) == (True, False, False, True, False, True)
    assert compare(a, a) == (True, False, False, True, False, True)


check_comparisons()


def check_direct_comparisons():
    a = bytearray(b"ab")
    for method in [bytearray.__eq__, bytearray.__ne__, bytearray.__lt__,
                   bytearray.__le__, bytearray.__gt__, bytearray.__ge__]:
        for other in [None, 1, 1.5, "ab", [97, 98], object()]:
            assert method(a, other) is NotImplemented
        try:
            method(b"ab", a)
        except TypeError:
            pass
        else:
            raise AssertionError
    assert bytearray.__lt__(a, b"ac") is True
    assert bytearray.__le__(a, a) is True
    assert bytearray.__gt__(a, b"a") is True
    assert bytearray.__ge__(a, b"ab") is True
    assert (a == object()) is False
    assert (a != object()) is True
    try:
        compare(a, object())
    except TypeError:
        pass
    else:
        raise AssertionError


check_direct_comparisons()


# Stored bytes subclasses use their payload on the RHS. A subclass method on
# the LHS and unrelated reflected methods must remain reachable.
class ReflectedBytes(bytes):
    def __lt__(self, other):
        return "normal-lt"
    def __gt__(self, other):
        return "reflected-gt"


class Other:
    def __gt__(self, other):
        return "other-gt"


def less(left, right):
    return left < right


for repeat in range(3):
    assert less(bytearray(b"a"), b"b") is True
for repeat in range(3):
    assert less(bytearray(b"a"), ReflectedBytes(b"b")) is True
    assert less(ReflectedBytes(b"a"), bytearray(b"b")) == "normal-lt"
for repeat in range(3):
    assert less(bytearray(b"a"), Other()) == "other-gt"
