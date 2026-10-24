# Independently written lifecycle regressions checked against CPython 3.14.5.
import operator


def raises(cls, function, *args):
    try:
        function(*args)
    except cls as error:
        return error
    assert False


def make_iterator(d, kind):
    if kind == 'dict':
        return iter(d)
    if kind == 'keys':
        return iter(d.keys())
    if kind == 'values':
        return iter(d.values())
    return iter(d.items())


def element(key, value, kind):
    if kind == 'values':
        return value
    if kind == 'items':
        return (key, value)
    return key


for kind in ('dict', 'keys', 'values', 'items'):
    d = {'a': 1, 'b': 2}
    i = make_iterator(d, kind)
    assert operator.length_hint(i, 77) == 2
    assert i.__length_hint__() == 2 and iter(i) is i
    assert next(i) == element('a', 1, kind)
    assert operator.length_hint(i) == 1
    # Shape-only promotion must not invalidate or reset the cursor/hint.
    assert d.get(42) is None
    d['b'] = 9
    assert operator.length_hint(i) == 1
    assert next(i) == element('b', 9, kind)
    assert operator.length_hint(i) == 0
    error = raises(StopIteration, next, i)
    assert error.args == () and error.value is None
    d['c'] = 3
    assert next(i, 'done') == 'done'
    d.clear()
    d['x'] = 4
    assert next(i, 'done') == 'done'
    assert operator.length_hint(i) == 0

    # Empty iterators detach only after an exhaustion attempt.
    d = {}
    i = make_iterator(d, kind)
    assert next(i, 'done') == 'done'
    d['a'] = 1
    assert next(i, 'done') == 'done'
    d = {'a': 1}
    i = make_iterator(d, kind)
    assert next(i) == element('a', 1, kind)
    d['b'] = 2
    error = raises(RuntimeError, next, i)
    assert str(error) == 'dictionary changed size during iteration'
    del d['b']
    raises(RuntimeError, next, i)
    assert operator.length_hint(i) == 0

    # Hints report temporary size mismatches without poisoning the iterator.
    d = {'a': 1, 'b': 2}
    i = make_iterator(d, kind)
    d['c'] = 3
    assert operator.length_hint(i) == 0
    del d['c']
    assert operator.length_hint(i) == 2
    assert next(i) == element('a', 1, kind)

    # Replacing an unseen key is accepted; replacing a seen key leaves an
    # unexpected entry after the original number of yields.
    d = {'a': 1, 'b': 2}
    i = make_iterator(d, kind)
    next(i)
    del d['b']
    d['c'] = 3
    assert next(i) == element('c', 3, kind)
    assert next(i, 'done') == 'done'
    d = {'a': 1, 'b': 2}
    i = make_iterator(d, kind)
    next(i)
    del d['a']
    d['c'] = 3
    assert next(i) == element('b', 2, kind)
    assert operator.length_hint(i) == 0
    error = raises(RuntimeError, next, i)
    assert str(error) == 'dictionary keys changed during iteration'
    d.clear()
    assert next(i, 'done') == 'done'
    d['later'] = 1
    assert next(i, 'done') == 'done'

    # Clear/reinsert at the same size follows the physical scan position;
    # generation changes alone do not imply a fail-fast error.
    d = {'a': 1, 'b': 2}
    i = make_iterator(d, kind)
    next(i)
    d.clear()
    d['x'] = 3
    d['y'] = 4
    assert next(i) == element('y', 4, kind)
    assert next(i, 'done') == 'done'

    # Cover native string operations and managed/general dict insertion.
    # 13 entries cross CloverVM's initial resize threshold, with a hole before
    # the iterator cursor. Value updates must not compact that hole away.
    for general in (False, True):
        for operation in ('assign', 'setitem', 'update', 'setdefault'):
            d = {}
            for n in range(13):
                d[str(n)] = n
            if general:
                d.get(42)
            del d['0']
            i = make_iterator(d, kind)
            assert next(i) == element('1', 1, kind)
            if operation == 'assign':
                d['12'] = 99
            elif operation == 'setitem':
                dict.__setitem__(d, '12', 99)
            elif operation == 'update':
                d.update({'12': 99})
            else:
                assert d.setdefault('12', 99) == 12
            assert next(i) == element('2', 2, kind)
            assert operator.length_hint(i) == 10
            assert len(list(i)) == 10
            assert next(i, 'done') == 'done'


# Iterator advancement and hints never rehash or compare stored keys.
class Key:
    def __init__(self, name):
        self.name = name

    def __hash__(self):
        events.append('hash')
        return 7

    def __eq__(self, other):
        events.append('eq')
        return self.name == other.name


events = []
a = Key('a')
b = Key('b')
d = {a: 1, b: 2}
for kind in ('dict', 'keys', 'values', 'items'):
    i = make_iterator(d, kind)
    events.clear()
    operator.length_hint(i)
    next(i)
    next(i)
    next(i, None)
    assert events == []

# A callback may mutate a dictionary and advance its existing iterator. The
# insertion probe must restart correctly and not apply universal generation
# invalidation to the iterator.
class ReentrantKey:
    def __init__(self, name):
        self.name = name

    def __hash__(self):
        return 11

    def __eq__(self, other):
        if mutate[0]:
            mutate[0] = False
            d['b'] = 20
            observed.append(next(i))
        return self.name == other.name


mutate = [False]
observed = []
a = ReentrantKey('a')
d = {a: 1, 'b': 2}
i = iter(d.values())
mutate[0] = True
d[ReentrantKey('a')] = 10
assert observed == [1]
assert next(i) == 20
assert next(i, None) is None

# Repeated direct calls retain native receiver and arity validation.
for kind in ('dict', 'keys', 'values', 'items'):
    i = make_iterator({'a': 1}, kind)
    try:
        i.__length_hint__(1)
    except TypeError:
        pass
    else:
        assert False
    try:
        i.__length_hint__(extra=1)
    except TypeError:
        pass
    else:
        assert False
    assert i.__length_hint__() == 1

# Growth after proving absence must not repeat equality callbacks. Exercise
# both managed assignment and native exact-dict update (reused stored hashes).
class CountingKey:
    def __init__(self, name):
        self.name = name

    def __hash__(self):
        return 42

    def __eq__(self, other):
        comparisons.append(self.name)
        return self.name == other.name


for operation in ('assign', 'update', 'setdefault'):
    comparisons = []
    d = {CountingKey('stored'): 1}
    for n in range(12):
        d[n] = n
    key = CountingKey('missing')
    source = {key: 99}
    comparisons.clear()
    if operation == 'assign':
        d[key] = 99
    elif operation == 'update':
        d.update(source)
    else:
        assert d.setdefault(key, 99) == 99
    assert comparisons == ['stored']
    assert len(d) == 14 and d[key] == 99

for kind in ('dict', 'keys', 'values', 'items'):
    i = make_iterator({'a': 1}, kind)
    raises(TypeError, i.__class__.__length_hint__, [])
    raises(TypeError, i.__class__.__next__, [])
    raises(TypeError, i.__class__.__iter__, [])
    assert next(i) == element('a', 1, kind)
