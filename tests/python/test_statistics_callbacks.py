# Original statistics callback regressions, checked against CPython 3.14.5.
import statistics as s


def raises(error, function, *args):
    try:
        function(*args)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


failure = RuntimeError("statistics callback failed")


class BadIter:
    def __iter__(self):
        raise failure


class BadNext:
    def __iter__(self):
        return self

    def __next__(self):
        raise failure


for function in (s.median, s.median_low, s.median_high, s.mode, s.multimode):
    assert raises(RuntimeError, function, BadIter()) is failure
    assert raises(RuntimeError, function, BadNext()) is failure


class BadOrder:
    def __lt__(self, other):
        raise failure


for function in (s.median, s.median_low, s.median_high):
    values = [BadOrder(), BadOrder()]
    first = values[0]
    second = values[1]
    assert raises(RuntimeError, function, values) is failure
    assert values[0] is first and values[1] is second


events = []


class Arithmetic:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return self.value < other.value

    def __add__(self, other):
        events.append("add")
        assert self.value == 1 and other.value == 2
        return self

    def __truediv__(self, other):
        events.append("divide")
        assert other == 2
        raise failure


left = Arithmetic(1)
right = Arithmetic(2)
assert raises(RuntimeError, s.median, [right, left]) is failure
assert events == ["add", "divide"]
events.clear()
assert s.median([left]) is left
assert s.median_low([right, left]) is left
assert s.median_high([right, left]) is right
assert events == []


class BadAdd(Arithmetic):
    def __add__(self, other):
        raise failure


assert raises(RuntimeError, s.median, [BadAdd(1), BadAdd(2)]) is failure


class Key:
    def __init__(self, value):
        self.value = value
        self.hashes = 0

    def __hash__(self):
        self.hashes += 1
        return 42

    def __eq__(self, other):
        return self.value == other.value


for function in (s.mode, s.multimode):
    first = Key(1)
    equal = Key(1)
    different = Key(2)
    result = function([first, different, equal, different])
    if function is s.mode:
        assert result is first
    else:
        assert len(result) == 2
        assert result[0] is first and result[1] is different
    assert first.hashes == 1 and equal.hashes == 1 and different.hashes == 2


class BadHash:
    def __hash__(self):
        raise failure


class BadEquality:
    def __hash__(self):
        return 42

    def __eq__(self, other):
        raise failure


class BadTruth:
    def __bool__(self):
        raise failure


class EqualityTruth:
    def __hash__(self):
        return 42

    def __eq__(self, other):
        return BadTruth()


for function in (s.mode, s.multimode):
    assert raises(RuntimeError, function, [BadHash()]) is failure
    assert raises(RuntimeError, function, [BadEquality(), BadEquality()]) is failure
    assert raises(RuntimeError, function, [EqualityTruth(), EqualityTruth()]) is failure


# Repeated identical keys must bypass equality, including raising __eq__.
key = BadEquality()
assert s.mode([key, key]) is key
assert s.multimode([key, key])[0] is key


# A callback can reenter the module without sharing counting state.
class Reentrant:
    def __hash__(self):
        assert s.mode([1, 2, 2]) == 2
        assert s.multimode([3, 1]) == [3, 1]
        return 42


key = Reentrant()
assert s.mode([key, key]) is key
assert s.multimode([key, key])[0] is key
