# Original statistics.quantiles callback regressions, checked against CPython 3.14.10.
import statistics as s


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError('expected exception')


failure = RuntimeError('quantiles callback failure')
events = []


class SinglePass:
    def __init__(self, values):
        self.values = values
        self.index = 0
        self.calls = 0

    def __iter__(self):
        self.calls += 1
        assert self.calls == 1
        events.append('iter')
        return self

    def __next__(self):
        events.append('next')
        if self.index == len(self.values):
            raise StopIteration
        value = self.values[self.index]
        self.index += 1
        return value


class Method:
    def __eq__(self, other):
        events.append(other)
        assert s.quantiles([0, 10], n=2) == [5.0]
        return False

    def __repr__(self):
        events.append('repr')
        return 'unknown'


for method in ('exclusive', 'inclusive'):
    source = SinglePass([30, 10, 20])
    assert s.quantiles(source, method=method) == s.quantiles([30, 10, 20], method=method)
    assert source.calls == 1 and source.index == 3

source = SinglePass([2, 1])
events.clear()
assert raises(s.StatisticsError, s.quantiles, source, n=0)
assert source.calls == 0 and events == []
raises(ValueError, s.quantiles, source, n=1, method=Method())
assert events == ['iter', 'next', 'next', 'next', 'inclusive', 'exclusive', 'repr']
for values in ([], [object()]):
    events.clear()
    source = SinglePass(values)
    if values:
        result = s.quantiles(source, method=Method())
        assert result[0] is values[0] and result[2] is values[0]
    else:
        raises(s.StatisticsError, s.quantiles, source, method=Method())
    assert 'inclusive' not in events and 'repr' not in events


class BadN:
    def __lt__(self, other):
        assert s.quantiles([0, 10], n=2) == [5.0]
        raise failure


assert raises(RuntimeError, s.quantiles, None, n=BadN()) is failure


class BadIter:
    def __iter__(self):
        raise failure


class BadNext:
    def __iter__(self):
        return self

    def __next__(self):
        raise failure


class BadOrder:
    def __lt__(self, other):
        raise failure


for method in ('exclusive', 'inclusive', 'bad'):
    assert raises(RuntimeError, s.quantiles, BadIter(), method=method) is failure
    assert raises(RuntimeError, s.quantiles, BadNext(), method=method) is failure
    assert raises(RuntimeError, s.quantiles, [BadOrder(), BadOrder()],
                  n=1, method=method) is failure


class BadMethod:
    def __eq__(self, other):
        raise failure


class BadRepr(Method):
    def __repr__(self):
        raise failure


assert raises(RuntimeError, s.quantiles, [1, 2], method=BadMethod()) is failure
assert raises(RuntimeError, s.quantiles, [1, 2], method=BadRepr()) is failure


# Arithmetic callbacks use independent objects and can reenter the function.
# These exercise existing dispatch rather than adding general numeric coercion.
class Number:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        assert s.quantiles([0, 10], n=2) == [5.0]
        return self.value < other.value

    def __mul__(self, weight):
        events.append('multiply')
        assert s.quantiles([0, 10], n=2, method='inclusive') == [5.0]
        return Number(self.value * weight)

    def __add__(self, other):
        events.append('add')
        return Number(self.value + other.value)

    def __truediv__(self, denominator):
        events.append('divide')
        return self.value / denominator


class FreshNumbers:
    def __init__(self):
        self.index = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.index == 2:
            raise StopIteration
        self.index += 1
        # The iterator retains no previously yielded object.
        return Number(self.index * 10)


for method in ('inclusive', 'exclusive'):
    for repeat in range(8):
        events.clear()
        assert s.quantiles(FreshNumbers(), n=2, method=method) == [15.0]
        assert events == ['multiply', 'multiply', 'add', 'divide']


class BadMultiply(Number):
    def __mul__(self, weight):
        raise failure


class BadAdd(Number):
    def __mul__(self, weight):
        return self

    def __add__(self, other):
        raise failure


class BadDivide(BadAdd):
    def __add__(self, other):
        return self

    def __truediv__(self, denominator):
        raise failure


for cls in (BadMultiply, BadAdd, BadDivide):
    for method in ('inclusive', 'exclusive'):
        assert raises(RuntimeError, s.quantiles, [cls(1), cls(2)], method=method) is failure


# Warm an arithmetic cache, then replace its class method.
class Changing(Number):
    def __mul__(self, weight):
        return self

    def __add__(self, other):
        return self

    def __truediv__(self, denominator):
        return 1.0


def replacement(self, denominator):
    return 2.0


assert s.quantiles([Changing(1), Changing(2)], n=2) == [1.0]
Changing.__truediv__ = replacement
assert s.quantiles([Changing(1), Changing(2)], n=2) == [2.0]

# A sorted copy retains values if comparison empties the caller's original list.
original = []


class ClearOriginal(Number):
    def __lt__(self, other):
        original.clear()
        return self.value < other.value


original.extend([ClearOriginal(20), ClearOriginal(10)])
assert s.quantiles(original, n=2) == [15.0]
assert original == []
