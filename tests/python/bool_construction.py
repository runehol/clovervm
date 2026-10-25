# Original bool construction regressions, checked against CPython 3.14.5.
assert bool() is False
assert bool.__new__(bool) is False
assert issubclass(bool, int)
assert isinstance(True, int)
for value in (None, False, 0, 0.0, "", b"", (), [], {}):
    assert bool(value) is False
    assert bool.__new__(bool, value) is False
for value in (True, 1, -1, 2**100, -2**100, 0.5, "x", b"x", (0,), [0], {0: 0}, object()):
    assert bool(value) is True
    assert bool.__new__(bool, value) is True


class IntTruth(int):
    def __bool__(self):
        return False


assert bool(IntTruth(7)) is False

calls = []


class Truth:
    def __bool__(self):
        calls.append("bool")
        return answer

    def __len__(self):
        raise AssertionError("must not fall back from __bool__")


truth = Truth()
for answer in (True, False):
    calls.clear()
    assert bool(truth) is answer
    assert calls == ["bool"]
    assert bool.__new__(bool, truth) is answer
    assert calls == ["bool", "bool"]
for answer in (None, 0, 1, "yes", NotImplemented):
    calls.clear()
    try:
        bool(truth)
    except TypeError:
        pass
    else:
        assert False
    assert calls == ["bool"]


class Length:
    def __len__(self):
        calls.append("len")
        return answer


class LengthInt(int):
    def __bool__(self):
        raise AssertionError("length payload must not be truth-converted again")


length = Length()
for answer in (False, True, 0, 1, 2**63 - 1, LengthInt(0), LengthInt(3)):
    calls.clear()
    assert bool(length) is (answer != 0)
    assert calls == ["len"]
for case in ((-1, ValueError), (2**63, OverflowError), (1.5, TypeError), (None, TypeError)):
    answer = case[0]
    exception = case[1]
    calls.clear()
    try:
        bool(length)
    except exception:
        pass
    else:
        assert False
    assert calls == ["len"]

failure = ValueError("truth failed")


class RaisingBool:
    def __bool__(self):
        raise failure


class RaisingLen:
    def __len__(self):
        raise failure


for value in (RaisingBool(), RaisingLen()):
    try:
        bool(value)
    except ValueError as error:
        assert error is failure
    else:
        assert False


class NotCallable:
    __bool__ = None

    def __len__(self):
        raise AssertionError("present __bool__ must not be skipped")


try:
    bool(NotCallable())
except TypeError:
    pass
else:
    assert False

# Bad argument lists and invalid direct receivers do not execute callbacks.
calls.clear()
for receiver in (None, 1, False, True, [], int, object):
    try:
        bool.__new__(receiver, truth)
    except TypeError:
        pass
    else:
        assert False
for mode in (0, 1, 2, 3, 4, 5):
    try:
        if mode == 0:
            bool(truth, truth)
        elif mode == 1:
            bool(object=truth)
        elif mode == 2:
            bool.__new__()
        elif mode == 3:
            bool.__new__(bool, truth, truth)
        elif mode == 4:
            bool.__new__(bool, object=truth)
        else:
            bool.__new__(cls=bool)
    except TypeError:
        pass
    else:
        assert False
assert calls == []
try:
    class Bad(bool):
        pass
except TypeError:
    pass
else:
    assert False

# All calls share the constructor's ToBool site. Warm each lookup before
# changing the class, including negative lookups and inherited methods.
class Base:
    pass


class Mutable(Base):
    pass


def convert(value):
    return bool(value)


def false_bool(self):
    return False


def zero_len(self):
    return 0


def one_len(self):
    return 1


value = Mutable()
# Special methods on individual instances do not participate in truth lookup.
value.__bool__ = false_bool
for repeat in range(4):
    assert convert(value) is True
Base.__len__ = zero_len
for repeat in range(4):
    assert convert(value) is False
Base.__len__ = one_len
for repeat in range(4):
    assert convert(value) is True
Mutable.__bool__ = false_bool
for repeat in range(4):
    assert convert(value) is False
del Mutable.__bool__
assert convert(value) is True
del Base.__len__
assert convert(value) is True

# Reentry replaces the shared constructor cache with a different result kind.
class Reentrant:
    def __bool__(self):
        assert bool(Length()) is False
        return True


answer = 0
assert bool(Reentrant()) is True


class ReentrantLength:
    def __len__(self):
        assert bool(Truth()) is False
        return 2


answer = False
assert bool(ReentrantLength()) is True
