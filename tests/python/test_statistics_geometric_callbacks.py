# Original streaming, error-ordering and reentry regressions, CPython 3.14.10.
import math
import statistics as s


def raises(error, function, *args):
    try:
        function(*args)
    except error as caught:
        return caught
    raise AssertionError('expected exception')


failure = RuntimeError('source failure')
events = []


class Source:
    def __init__(self, values, fail=False):
        self.values = values
        self.index = 0
        self.fail = fail
        self.calls = 0

    def __iter__(self):
        self.calls += 1
        assert self.calls == 1
        events.append('iter')
        return self

    def __next__(self):
        events.append(self.index)
        if self.index == len(self.values):
            if self.fail:
                raise failure
            raise StopIteration
        value = self.values[self.index]
        self.index += 1
        return value


source = Source([4, 9])
assert s.geometric_mean(source) == 6.0
assert events == ['iter', 0, 1, 2]
for values in ([0, 1], [math.nan, 1], [math.inf, 1], [10**1000, 1]):
    # No early zero/NaN/infinity/exp-overflow return may hide a source error.
    assert raises(RuntimeError, s.geometric_mean, Source(values, True)) is failure
for case in (([0, -1, None], s.StatisticsError),
             ([math.nan, None, -1], TypeError),
             ([math.inf, -10**400, None], OverflowError)):
    values = case[0]
    error = case[1]
    events.clear()
    source = Source(values, True)
    raises(error, s.geometric_mean, source)
    assert events == ['iter', 0, 1]


class BadIter:
    def __iter__(self):
        raise failure


assert raises(RuntimeError, s.geometric_mean, BadIter()) is failure


class StopIter:
    def __iter__(self):
        raise StopIteration('acquisition')


assert raises(RuntimeError, s.geometric_mean, StopIter()).args == (
    'generator raised StopIteration',)


class Reentrant(int):
    def __gt__(self, other):
        events.append('gt')
        assert s.geometric_mean([4, 9]) == 6.0
        return int.__gt__(self, other)


for i in range(100):
    assert s.geometric_mean([Reentrant(4), 9]) == 6.0


class Changing(int):
    def __gt__(self, other):
        return True


def reject(self, other):
    raise failure


value = Changing(4)
assert s.geometric_mean([value, 9]) == 6.0
Changing.__gt__ = reject
assert raises(RuntimeError, s.geometric_mean, [value]) is failure


class StopComparison(int):
    def __gt__(self, other):
        raise StopIteration('comparison')


class StopEquality(int):
    def __eq__(self, other):
        raise StopIteration('equality')


for value in (StopComparison(1), StopEquality(0)):
    assert raises(RuntimeError, s.geometric_mean, [value]).args == (
        'generator raised StopIteration',)


class ComparisonResult:
    def __bool__(self):
        raise failure


class BadTruth(int):
    def __gt__(self, other):
        return ComparisonResult()


assert raises(RuntimeError, s.geometric_mean, [BadTruth(4)]) is failure


class TruthOnce:
    def __init__(self):
        self.calls = 0

    def __bool__(self):
        self.calls += 1
        assert self.calls == 1
        return True


truth = TruthOnce()


class TruthComparison(int):
    def __gt__(self, other):
        return truth


assert s.geometric_mean([TruthComparison(4), 9]) == 6.0
assert truth.calls == 1


class Ephemeral:
    def __init__(self):
        self.index = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.index == 20:
            raise StopIteration
        self.index += 1
        return Reentrant(1)


assert s.geometric_mean(Ephemeral()) == 1.0
