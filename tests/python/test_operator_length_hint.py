# Tests for CloverVM's supported operator.length_hint protocol.
# Behavior checked against CPython 3.14; no CPython test code copied.

import operator


class Hint:
    def __init__(self, result):
        self.result = result
        self.calls = 0

    def __length_hint__(self):
        self.calls += 1
        return self.result


class Both(Hint):
    def __len__(self):
        return 4


class TypeErrorLength(Hint):
    def __len__(self):
        raise TypeError("unavailable")


class InvalidLength(Hint):
    def __len__(self):
        return "bad"


class TypeErrorHint:
    def __length_hint__(self):
        raise TypeError("unavailable")


class ValueErrorHint:
    def __length_hint__(self):
        raise ValueError("hint callback")


class ValueErrorLength(Hint):
    def __len__(self):
        raise ValueError("length callback")


class LengthResult(Hint):
    def __len__(self):
        return self.result


class I(int):
    pass


# Returned values normalize bools/subclasses, but retain large valid integers.
for result in (0, 5, True, False, I(5), I(0), 10**18, 2**63 - 1):
    obj = Hint(result)
    value = operator.length_hint(obj, 9)
    assert value == int(result)
    assert value.__class__ is int
    assert obj.calls == 1

obj = Both(8)
assert operator.length_hint(obj) == 4
assert obj.calls == 0
assert operator.length_hint(TypeErrorLength(8)) == 8
assert operator.length_hint(InvalidLength(8)) == 8
for obj in (42, Hint(NotImplemented), TypeErrorHint()):
    assert operator.length_hint(obj, 9) == 9

for default in (True, False, I(7), -1, -(2**63), 2**63 - 1, 10**18):
    value = operator.length_hint(42, default)
    assert value == int(default)
    assert value.__class__ is int

# Invalid defaults fail before either callback, even when length is available.
for obj in (Hint(5), Both(5), ValueErrorLength(5)):
    for default in ("bad", None, 1.5):
        try:
            operator.length_hint(obj, default)
        except TypeError:
            pass
        else:
            raise AssertionError("invalid default accepted")
    for default in (2**63, -(2**63) - 1):
        try:
            operator.length_hint(obj, default)
        except OverflowError:
            pass
        else:
            raise AssertionError("oversized default accepted")
    assert obj.calls == 0

# Invalid returned hints raise outside the callback-TypeError fallback.
for result in (None, "bad", 1.5):
    try:
        operator.length_hint(Hint(result), 9)
    except TypeError:
        pass
    else:
        raise AssertionError("invalid hint accepted")
for result in (-1, I(-1)):
    try:
        operator.length_hint(Hint(result), 9)
    except ValueError:
        pass
    else:
        raise AssertionError("negative hint accepted")
for result in (2**63, -(2**63) - 1):
    try:
        operator.length_hint(Hint(result), 9)
    except OverflowError:
        pass
    else:
        raise AssertionError("oversized hint accepted")

for obj in (ValueErrorHint(), ValueErrorLength(5)):
    try:
        operator.length_hint(obj)
    except ValueError:
        pass
    else:
        raise AssertionError("callback exception swallowed")

# Local validation compensates for len()'s currently unchecked return values.
for result in (-1, -(2**100)):
    try:
        operator.length_hint(LengthResult(result))
    except ValueError:
        pass
    else:
        raise AssertionError("negative length accepted")
try:
    operator.length_hint(LengthResult(2**63))
except OverflowError:
    pass
else:
    raise AssertionError("oversized length accepted")
assert operator.length_hint(LengthResult(True)) == 1
assert operator.length_hint(LengthResult(10**18)) == 10**18

# Special lookup ignores instance attributes and observes class mutation.
obj = Hint(5)
obj.__length_hint__ = None
assert operator.length_hint(obj) == 5


def replacement(self):
    return 11


for i in range(10):
    assert operator.length_hint(obj) == 5
Hint.__length_hint__ = replacement
assert operator.length_hint(obj) == 11


def invalid_replacement(self):
    return "bad"


Hint.__length_hint__ = invalid_replacement
try:
    operator.length_hint(obj, 9)
except TypeError:
    pass
else:
    raise AssertionError("cached old method after replacement")
del Hint.__length_hint__
assert operator.length_hint(obj, 9) == 9

# CPython's call convention is positional-only.
try:
    operator.length_hint(42, default=9)
except TypeError:
    pass
else:
    raise AssertionError("keyword accepted")
