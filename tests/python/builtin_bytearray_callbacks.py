# Original callback/alias regression cases, reference CPython 3.14.5.
class Source:
    def __init__(self, target, values):
        self.target = target
        self.values = values
    def __iter__(self):
        self.target.append(88)
        return iter(self.values)

a = bytearray(b"ab")
a.extend(Source(a, [67, 68]))
assert a == b"abXCD"
a = bytearray(b"ab")
try:
    a.extend(Source(a, [67, 999]))
except ValueError:
    pass
else:
    raise AssertionError
assert a == b"abX"
a = bytearray(b"ab")
try:
    a[:] = Source(a, [67, 999])
except ValueError:
    pass
else:
    raise AssertionError
assert a == b"abX"
a = bytearray(b"abc")
a[1:] = Source(a, [67])
assert a == b"aC"
a = bytearray(b"ab")
try:
    a[::0] = Source(a, [67])
except ValueError:
    pass
else:
    raise AssertionError
assert a == b"ab"

class Failing:
    def __iter__(self):
        return self
    def __next__(self):
        raise RuntimeError("iterator failed")
for operation in [0, 1, 2]:
    a = bytearray(b"ab")
    try:
        if operation == 0:
            a.extend(Failing())
        elif operation == 1:
            a[:] = Failing()
        else:
            bytearray(Failing())
    except RuntimeError:
        pass
    else:
        raise AssertionError
    assert a == b"ab"

class Reallocate:
    def __init__(self, target):
        self.target = target
    def __iter__(self):
        self.target.clear()
        self.target.extend(range(256))
        return iter([65, 66])
a = bytearray(b"old")
a.extend(Reallocate(a))
assert len(a) == 258 and a[:3] == b"\x00\x01\x02" and a[-2:] == b"AB"
a[:] = Reallocate(a)
assert a == b"AB"

# A live iterator follows the owner through backing replacement.
a = bytearray(b"ab")
it = iter(a)
assert next(it) == 97
a.extend(range(256))
assert next(it) == 98
assert next(it) == 0
it = iter(bytearray([7, 8]))
assert next(it) == 7 and next(it) == 8


# Slice assignment rejects stored numeric payloads before consuming input.
# Constructor and extend still accept a float subclass's integer iterable.
class NumericSliceSource(float):
    def __iter__(self):
        numeric_slice_events.append(1)
        numeric_slice_target.append(88)
        return iter([65])


def check_numeric_slice_sources():
    global numeric_slice_events, numeric_slice_target
    numeric_slice_events = []
    numeric_slice_target = bytearray(b"abc")
    for index in [slice(None), slice(None, None, 2)]:
        try:
            numeric_slice_target[index] = NumericSliceSource(1)
        except TypeError:
            pass
        else:
            raise AssertionError
        assert numeric_slice_events == [] and numeric_slice_target == b"abc"
    try:
        numeric_slice_target[::0] = NumericSliceSource(1)
    except ValueError:
        pass
    else:
        raise AssertionError
    assert numeric_slice_events == [] and numeric_slice_target == b"abc"
    assert bytearray(NumericSliceSource(1)) == b"A"
    assert numeric_slice_events == [1] and numeric_slice_target == b"abcX"
    numeric_slice_target.extend(NumericSliceSource(1))
    assert numeric_slice_events == [1, 1] and numeric_slice_target == b"abcXXA"


check_numeric_slice_sources()


def check_empty_slice_source_mutation():
    target = bytearray(b"abc")
    target[::2] = Source(target, [])
    assert target == b"bX"
    target = bytearray(b"abc")
    target[::-2] = Source(target, [])
    assert target == b"ac"


check_empty_slice_source_mutation()


# One cached setter sees all RHS kinds; the value shape is not an IC guard.
def assign_slice(target, key, value):
    target[key] = value


def assign_slice_method(target, key, value):
    target.__setitem__(key, value)


def check_cached_slice_assignment(assign):
    for key in [slice(None), slice(None, None, -1)]:
        target = bytearray(b"old")
        for value in [b"ABC", bytearray(b"DEF"), [71, 72, 73],
                      iter([74, 75, 76]), b"MNO"]:
            assign(target, key, value)
        assert target == (b"ONM" if key.step == -1 else b"MNO")
        assign(target, key, target)
        assert target == b"MNO"
        try:
            assign(target, key, [65, 999])
        except ValueError:
            pass
        else:
            raise AssertionError
        assert target == b"MNO"
        try:
            assign(target, key, 3)
        except TypeError:
            pass
        else:
            raise AssertionError
        assert target == b"MNO"
        assign(target, key, Source(target, [80, 81, 82, 83]))
        assert target == (b"SRQP" if key.step == -1 else b"PQRS")
        assign(target, key, [])
        assert target == b""


check_cached_slice_assignment(assign_slice)
check_cached_slice_assignment(assign_slice_method)
