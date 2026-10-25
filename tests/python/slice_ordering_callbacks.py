# CPython 3.14.5 slice_richcompare delegates to tuple_richcompare:
# equality is truth-tested; the selected ordering result is returned unchanged.
trace = []
result = object()


class Truth:
    def __init__(self, label, value):
        self.label = label
        self.value = value

    def __bool__(self):
        trace.append('bool ' + self.label)
        return self.value


class Field:
    def __init__(self, label, equal):
        self.label = label
        self.equal = equal

    def __eq__(self, other):
        trace.append('eq ' + self.label)
        return Truth(self.label, self.equal)

    def __lt__(self, other):
        trace.append('lt ' + self.label)
        return result

    def __le__(self, other):
        trace.append('le ' + self.label)
        return result

    def __gt__(self, other):
        trace.append('gt ' + self.label)
        return result

    def __ge__(self, other):
        trace.append('ge ' + self.label)
        return result

    def __index__(self):
        raise AssertionError('index must not run')


for pair in ((slice.__lt__, 'lt'), (slice.__le__, 'le'),
                     (slice.__gt__, 'gt'), (slice.__ge__, 'ge')):
    method = pair[0]
    name = pair[1]
    for failed in range(3):
        trace.clear()
        left = slice(Field('start', failed != 0), Field('stop', failed != 1), Field('step', failed != 2))
        right = slice(Field('right', True), Field('right', True), Field('right', True))
        assert method(left, right) is result
        expected = []
        for label in ('start', 'stop', 'step')[:failed + 1]:
            expected.append('eq ' + label)
            expected.append('bool ' + label)
        expected.append(name + ' ' + ('start', 'stop', 'step')[failed])
        assert trace == expected


class Left:
    def __eq__(self, other):
        trace.append('left eq')
        return NotImplemented

    def __lt__(self, other):
        trace.append('left lt')
        return NotImplemented


class Right:
    def __eq__(self, other):
        trace.append('right eq')
        return False

    def __gt__(self, other):
        trace.append('right gt')
        return result


class Sub(Left):
    def __eq__(self, other):
        trace.append('sub eq')
        return False

    def __gt__(self, other):
        trace.append('sub gt')
        return result


trace.clear()
assert (slice(Left()) < slice(Right())) is result
assert trace == ['left eq', 'right eq', 'left lt', 'right gt']
trace.clear()
assert (slice(Left()) < slice(Sub())) is result
assert trace == ['sub eq', 'sub gt']
# Non-slice outer reflection must still be reached after NotImplemented.
assert (slice(1) < Right()) is result


def raises(kind, fn, *args):
    try:
        fn(*args)
    except kind:
        return
    assert False, 'expected exception'


class BadEqual:
    def __eq__(self, other):
        raise ValueError('equality callback')


class BadTruth:
    def __bool__(self):
        raise RuntimeError('truth callback')


class BadTruthField:
    def __eq__(self, other):
        return BadTruth()


class BadOrder:
    def __eq__(self, other):
        return False

    def __lt__(self, other):
        raise LookupError('ordering callback')


for method in (slice.__lt__, slice.__le__, slice.__gt__, slice.__ge__):
    raises(ValueError, method, slice(BadEqual()), slice(BadEqual()))
    raises(RuntimeError, method, slice(BadTruthField()), slice(BadTruthField()))
raises(LookupError, slice.__lt__, slice(BadOrder()), slice(BadOrder()))


class Outer:
    def __gt__(self, other):
        return 'outer gt'

    def __ge__(self, other):
        return 'outer ge'

    def __lt__(self, other):
        return 'outer lt'

    def __le__(self, other):
        return 'outer le'


assert (slice(1) < Outer()) == 'outer gt'
assert (slice(1) <= Outer()) == 'outer ge'
assert (slice(1) > Outer()) == 'outer lt'
assert (slice(1) >= Outer()) == 'outer le'


class LenTruth:
    def __len__(self):
        return 0


class LenField:
    def __eq__(self, other):
        return LenTruth()

    def __lt__(self, other):
        return result


assert (slice(LenField()) < slice(LenField())) is result


def invalid_bool(self):
    return 1


LenTruth.__bool__ = invalid_bool
raises(TypeError, slice.__lt__, slice(LenField()), slice(LenField()))
del LenTruth.__bool__


def negative_len(self):
    return -1


LenTruth.__len__ = negative_len
raises(ValueError, slice.__lt__, slice(LenField()), slice(LenField()))
