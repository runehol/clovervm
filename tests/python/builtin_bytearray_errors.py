# Original CPython 3.14.5 error and failure-atomicity regressions.
huge = 10 ** 100
for count in [-1, -10]:
    try:
        bytearray(count)
    except ValueError:
        pass
    else:
        raise AssertionError
for count in [huge, -huge]:
    try:
        bytearray(count)
    except OverflowError:
        pass
    else:
        raise AssertionError
for value in [-1, 256, huge, -huge]:
    try:
        bytearray([value])
    except ValueError:
        pass
    else:
        raise AssertionError
    a = bytearray(b"ab")
    try:
        a.append(value)
    except ValueError:
        pass
    else:
        raise AssertionError
    assert a == b"ab"
    try:
        a[0] = value
    except ValueError:
        pass
    else:
        raise AssertionError
    assert a == b"ab"
for index in [2, -3, huge, -huge]:
    a = bytearray(b"ab")
    try:
        a[index]
    except IndexError:
        pass
    else:
        raise AssertionError
    try:
        a[index] = 65
    except IndexError:
        pass
    else:
        raise AssertionError
    try:
        del a[index]
    except IndexError:
        pass
    else:
        raise AssertionError
    assert a == b"ab"
a = bytearray(b"ab")
for value in [1, "", "x", None]:
    try:
        a[:] = value
    except TypeError:
        pass
    else:
        raise AssertionError
    assert a == b"ab"
try:
    a[::2] = b"xyz"
except ValueError:
    pass
else:
    raise AssertionError
assert a == b"ab"
for value in ["", "abc", None, 1.5]:
    try:
        bytearray(value)
    except TypeError:
        pass
    else:
        raise AssertionError
try:
    hash(a)
except TypeError:
    pass
else:
    raise AssertionError
assert bytearray.__hash__ is None
try:
    {a: 1}
except TypeError:
    pass
else:
    raise AssertionError

# Conversion/type errors precede byte conversion; platform-sized bounds errors
# follow it. None of these failures may mutate the receiver.
a = bytearray(b"ab")
for index in ["x", 1.5]:
    try:
        a[index] = 999
    except TypeError:
        pass
    else:
        raise AssertionError
for index in [huge, -huge]:
    try:
        a[index] = 999
    except IndexError:
        pass
    else:
        raise AssertionError
try:
    a[100] = 999
except ValueError:
    pass
else:
    raise AssertionError
assert a == b"ab"
