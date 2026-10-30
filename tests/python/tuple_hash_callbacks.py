# Callback semantics shared with CPython 3.14.5. Use fresh tuples because that
# implementation caches completed tuple hashes, while Clover recomputes them.
def hash_one(value):
    return hash(value)


class Element:
    def __hash__(self):
        return 3


def replacement(self):
    return 5


for i in range(4):
    assert hash_one((Element(),)) == hash_one((3,))
Element.__hash__ = replacement
for i in range(4):
    assert hash_one((Element(),)) == hash_one((5,))

# A callback can invalidate the method used at the very next element hash site.
class Later:
    def __hash__(self):
        return 3


class Change:
    def __hash__(self):
        Later.__hash__ = replacement
        return 1


for i in range(4):
    def original(self):
        return 3
    Later.__hash__ = original
    assert hash_one((Change(), Later())) == hash_one((1, 5))

marker = ValueError('hash callback failed')
class Fails:
    def __hash__(self):
        raise marker


class NeverHash:
    def __hash__(self):
        raise AssertionError('hashing must stop at the first exception')


for i in range(4):
    try:
        hash_one((1, Fails(), NeverHash()))
    except ValueError as error:
        assert error is marker
    else:
        raise AssertionError('hash exception swallowed')
    assert hash_one((1, 2)) == tuple.__hash__((1, 2))

# Hashing keeps the receiver, its current element, and later elements alive when
# callbacks drop all external references and allocate/reenter the same sites.
held = None
class Drop:
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        global held
        held = None
        garbage = []
        for i in range(40):
            garbage.append(([i], [i + 1]))
        assert hash_one((1, (2,))) == hash_one((1.0, (2.0,)))
        assert self.value == 11
        return self.value


class Tail:
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        assert self.value == 12
        return self.value


class Payload(tuple):
    def __getitem__(self, index):
        raise AssertionError('stored items expected')


for cls in [tuple, Payload]:
    for i in range(4):
        held = cls((Drop(11), Tail(12)))
        assert hash_one(held) == hash_one((11, 12))
        assert held is None

# Bounded reentry on the very same tuple has independent accumulator state.
active_tuple = None
class Reenter:
    def __init__(self):
        self.busy = False

    def __hash__(self):
        if not self.busy:
            self.busy = True
            assert hash_one(active_tuple) == hash_one((13, 14))
            self.busy = False
        return 13


for i in range(4):
    active_tuple = (Reenter(), 14)
    assert hash_one(active_tuple) == hash_one((13, 14))
active_tuple = None

# Dictionary hashing must tolerate callback mutation before probing begins.
mapping = {}
class MutatesDict:
    def __init__(self):
        self.first = True

    def __hash__(self):
        if self.first:
            mapping.clear()
            self.first = False
        for i in range(30):
            mapping[i] = i
        return 19


for i in range(4):
    key = (MutatesDict(),)
    mapping[key] = 'stored'
    assert mapping[(key[0],)] == 'stored'
    assert len(mapping) == 31

# Tuple-subclass hash lookup still uses ordinary cache invalidation.
class Sub(tuple):
    def __hash__(self):
        return 23


value = Sub((1,))
for i in range(4):
    assert hash_one(value) == 23
Sub.__hash__ = replacement
for i in range(4):
    assert hash_one(value) == 5
del Sub.__hash__
for i in range(4):
    assert hash_one(value) == hash_one((1,))
