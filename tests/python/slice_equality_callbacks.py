# Slice field comparison order, reflection, and truth conversion.
def raises(kind, fn, *args):
    try:
        fn(*args)
    except kind:
        return
    assert False, 'expected exception'


def equal(left, right):
    return left == right


def unequal(left, right):
    return left != right


trace = []


class Truth:
    def __init__(self, label, result):
        self.label = label
        self.result = result

    def __bool__(self):
        trace.append('bool ' + self.label)
        return self.result


class Field:
    def __init__(self, label, result):
        self.label = label
        self.result = result

    def __eq__(self, other):
        trace.append(self.label)
        return Truth(self.label, self.result)

    def __ne__(self, other):
        raise AssertionError('field inequality')


for fn in (equal, unequal, slice.__eq__, slice.__ne__):
    for failed in (0, 1, 2, 3):
        trace.clear()
        a = slice(Field('start', failed != 0), Field('stop', failed != 1), Field('step', failed != 2))
        b = slice(Field('right', True), Field('right', True), Field('right', True))
        result = fn(a, b)
        assert result is (failed == 3 if fn in (equal, slice.__eq__) else failed != 3)
        expected = []
        for label in ('start', 'stop', 'step')[:min(failed + 1, 3)]:
            expected.append(label)
            expected.append('bool ' + label)
        assert trace == expected


class LenTruth:
    def __len__(self):
        return 1


class ResultField:
    def __eq__(self, other):
        return LenTruth()


a = slice(ResultField())
b = slice(ResultField())
assert equal(a, b) and not unequal(a, b)
def zero_len(self):
    return 0


LenTruth.__len__ = zero_len
assert not equal(a, b) and unequal(a, b)


def bad_len(self):
    raise ValueError('truth')


LenTruth.__len__ = bad_len
for fn in (equal, unequal):
    raises(ValueError, fn, a, b)


def negative_len(self):
    return -1


LenTruth.__len__ = negative_len
raises(ValueError, equal, a, b)


def bad_bool(self):
    return 1


LenTruth.__bool__ = bad_bool
raises(TypeError, equal, a, b)
del LenTruth.__bool__

# Subclass priority and reflected NotImplemented continuation apply to fields.
class Left:
    def __eq__(self, other):
        trace.append('left')
        return NotImplemented


class Right:
    def __eq__(self, other):
        trace.append('right')
        return True


class Sub(Left):
    def __eq__(self, other):
        trace.append('sub')
        return True


trace.clear()
assert slice(Left()) == slice(Right())
assert trace == ['left', 'right']
trace.clear()
assert slice(Left()) == slice(Sub())
assert trace == ['sub']

