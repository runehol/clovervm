import math
import statistics as s


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


assert s.variance([1, 3]) == 2
assert s.variance([1, 3], xbar=0) == 10
assert s.pvariance(data=[1, 3], mu=0) == 5
assert s.pvariance([1, 3], 0.0).__class__ is float
assert s.variance([1, 3], None).__class__ is int
assert s.pvariance([True]).__class__ is int
assert s.pvariance([True, False]) == 0.25
assert s.pvariance([1, 2.0]) == 0.25
assert s.stdev(data=[3, 4], xbar=0) == 5.0
assert s.pstdev([3, 3], mu=0) == 3.0
assert s.pstdev([False]).__class__ is float

offset = 10**1000
assert s.variance([offset, offset + 2]) == 2
assert s.pvariance([offset, offset + 2]) == 1
assert s.stdev([offset, offset + 2]) == 1.4142135623730951
assert s.pvariance([0, offset]) == (offset * offset) // 4
assert s.pstdev([0, 10**200]) == 5e199
raises(OverflowError, s.pstdev, [0, offset])
raises(OverflowError, s.pvariance, [0.0, 1e200])
raises(OverflowError, s.pvariance, [offset, 1.5], 0.0)

tiny = 5e-324
assert s.pvariance([1.0, 1.0000000000000002]) == 1.232595164407831e-32
assert s.variance([0.0, tiny]) == 0.0
assert s.stdev([0.0, tiny]) == tiny
assert s.stdev([0.0, tiny], 0.0) == 0.0
assert s.pstdev([0.0, tiny]) == 0.0
assert s.pstdev([0.0, 1.5e-323]) == 1e-323

for name in ("variance", "pvariance", "stdev", "pstdev"):
    assert name in s.__all__

for f in (s.variance, s.pvariance, s.stdev, s.pstdev):
    raises(TypeError, f)
    raises(TypeError, f, [1, 2], None, None)
    raises(TypeError, f, [1, 2], extra=0)
    raises(TypeError, f, [1, 2], None, data=[1, 2])
    raises(TypeError, f, [1, 2], "0")
    raises(TypeError, f, [math.nan, "2"])
    raises(s.StatisticsError, f, [], "unused center")
    assert math.copysign(1.0, f([-0.0, -0.0])) == 1.0

for entry in ((s.variance, "variance"), (s.stdev, "stdev")):
    f = entry[0]
    raises(TypeError, f, [1, 2], mu=0)
    assert raises(s.StatisticsError, f, [math.nan]).args == (
        entry[1] + " requires at least two data points",)

for entry in ((s.pvariance, "pvariance"), (s.pstdev, "pstdev")):
    f = entry[0]
    raises(TypeError, f, [1, 2], xbar=0)
    assert raises(s.StatisticsError, f, []).args == (
        entry[1] + " requires at least one data point",)

for f in (s.variance, s.pvariance):
    assert f([1.0, math.inf]) == math.inf
    assert f([-math.inf, 1.0]) == -math.inf
    assert math.isnan(f([math.inf, -math.inf]))
    assert math.isnan(f([math.nan, 1.0]))
    assert f([math.inf, -math.inf], 0.0) == math.inf
    assert f([1e200, 1e200], 0.0) == math.inf
    assert math.isnan(f([1.0, math.inf], math.inf))

for f in (s.stdev, s.pstdev):
    for values in ([1.0, math.inf], [-math.inf, 1.0], [math.nan, 1.0]):
        assert raises(ValueError, f, values).args == (
            "inf or nan encountered in data",)
    raises(ValueError, f, [1e200, 1e200], 0.0)


failure = LookupError("source failure")
stop = StopIteration("source acquisition")


class BadIter:
    def __iter__(self):
        raise stop


class Source:
    def __init__(self, values, fail=False):
        self.values = values
        self.fail = fail
        self.index = 0
        self.calls = 0
        self.advances = 0

    def __iter__(self):
        self.calls += 1
        assert self.calls == 1
        return self

    def __next__(self):
        self.advances += 1
        if self.index == len(self.values):
            if self.fail:
                raise failure
            raise StopIteration
        assert s.pvariance([0, 2]) == 1
        value = self.values[self.index]
        self.index += 1
        return value


for f in (s.variance, s.pvariance, s.stdev, s.pstdev):
    for center in (None, 1):
        source = Source([0, 1, 2])
        assert f(source, center) == f([0, 1, 2], center)
        assert source.calls == 1
        assert source.index == 3
        # CPython's uncentered groupby can probe exhaustion twice.
        assert source.advances in (4, 5) if center is None else source.advances == 4
        assert raises(StopIteration, f, BadIter(), center) is stop
        for values in ([math.nan, 1], [math.inf, 1], [0, offset]):
            source = Source(values, True)
            assert raises(LookupError, f, source, center) is failure
            assert source.advances == 3
        source = Source([math.nan, "bad"], True)
        raises(TypeError, f, source, center)
        assert source.advances == 2
    source = Source([1], True)
    raises(TypeError, f, source, "invalid center")
    assert source.advances == 1
