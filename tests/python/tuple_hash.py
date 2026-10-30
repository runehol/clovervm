# Semantic assertions shared with CPython 3.14.5; hash numbers are VM-specific.
def hash_one(value):
    return hash(value)


def lookup(mapping, key):
    return mapping[key]


for i in range(4):
    for value in [(), (1,), (1, 2), ((), (1, (2,))), (None, 'text', b'bytes')]:
        assert tuple.__hash__(value) == hash_one(value)
        assert hash_one(value) != -1
        assert lookup({value: 'found'}, value) == 'found'
    assert hash_one(17) == 17
    assert hash_one(True) == 1
    assert hash_one(None) == hash(None)
    assert hash_one(()) == tuple.__hash__(())

# Equal values must remain interchangeable keys across numeric representations.
for pair in [((True,), (1.0,)), ((False, -1), (-0.0, -1.0)),
             ((2**60,), (float(2**60),)), ((2**100,), (float(2**100),)),
             ((-2**100,), (float(-2**100),)),
             ((1, (2, (False,))), (1.0, (2.0, (0,))))]:
    left = pair[0]
    right = pair[1]
    assert left == right
    assert hash_one(left) == hash_one(right)
    mapping = {left: 1}
    assert lookup(mapping, right) == 1
    mapping[right] = 2
    assert len(mapping) == 1
    assert mapping[left] == 2
    assert right in mapping
    assert mapping.pop(right) == 2
    assert len(mapping) == 0

nan = float('nan')
assert hash((nan,)) == hash((nan,))
assert {(nan,): 1}[(nan,)] == 1

class Payload(tuple):
    def __len__(self):
        raise AssertionError('hash must use stored size')

    def __iter__(self):
        raise AssertionError('hash must use stored elements')

    def __getitem__(self, index):
        raise AssertionError('hash must use stored elements')


class Child(Payload):
    pass


for i in range(4):
    value = Child((1, (2,)))
    assert hash_one(value) == hash_one((1, (2,)))
    assert tuple.__hash__(value) == hash_one(value)
    assert {(1.0, (2.0,)): 7}[value] == 7

class Override(Payload):
    def __hash__(self):
        return 91


value = Override((1,))
assert hash_one(value) == 91
assert tuple.__hash__(value) == hash_one((1,))
assert {value: 'override'}[value] == 'override'

class Disabled(Payload):
    __hash__ = None


class InvalidHash:
    def __hash__(self):
        return 'invalid'


for bad in [([],), ({},), (bytearray(),), (([],),), (InvalidHash(),), Disabled((1,))]:
    for i in range(3):
        try:
            hash_one(bad)
        except TypeError:
            pass
        else:
            raise AssertionError('unhashable accepted')
        mapping = {'kept': 1}
        try:
            mapping[bad] = 2
        except TypeError:
            pass
        else:
            raise AssertionError('unhashable key accepted')
        assert len(mapping) == 1
        assert mapping['kept'] == 1

for value in [0, True, None, [], {}, 'text', b'bytes']:
    try:
        tuple.__hash__(value)
    except TypeError:
        pass
    else:
        raise AssertionError('wrong receiver accepted')

for args in [(), ((), 1)]:
    try:
        tuple.__hash__(*args)
    except TypeError:
        pass
    else:
        raise AssertionError('wrong arity accepted')

# Canonicalization of user hash results remains shared with ordinary hashing.
class HashResult:
    def __init__(self, result):
        self.result = result

    def __hash__(self):
        return self.result


for result in [True, False, -1, -2, 0, 2**100, -2**100]:
    assert hash((HashResult(result),)) == hash((result,))

# Colliding elements must leave unequal tuples distinct in dictionary probes.
class Collision:
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        return 7

    def __eq__(self, other):
        return self.value == other.value


first = (Collision(1),)
second = (Collision(2),)
assert hash(first) == hash(second)
mapping = {first: 'one', second: 'two'}
assert len(mapping) == 2
for i in range(4):
    assert lookup(mapping, (Collision(1),)) == 'one'
    assert lookup(mapping, (Collision(2),)) == 'two'
