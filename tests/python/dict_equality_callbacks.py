# Structural dictionary comparison uses stored hashes and managed callbacks.
from collections import UserDict


class Key:
    def __init__(self):
        self.calls = 0
        self.limit = 100

    def __hash__(self):
        self.calls += 1
        if self.calls > self.limit:
            raise ValueError('extra hash')
        return 42


key = Key()
u = UserDict()
u.data[key] = 1
other = {key: 1}
key.calls = 0
key.limit = 4
assert u == other and key.calls == 4
key.calls = 0
key.limit = 0
assert u.data == other and key.calls == 0
assert not u.data != other and key.calls == 0
assert {'a': {'b': 1}} == {'a': {'b': 1}}
assert UserDict(a={'b': 1}) == {'a': {'b': 1}}
assert {'a': 1} != {'a': 2}
assert {'a': 1} != {'b': 1}
assert {'a': 1} != {'a': 1, 'b': 2}
assert dict.__eq__({}, []) is NotImplemented
assert dict.__ne__({}, []) is NotImplemented


class PayloadDict(dict):
    def __eq__(self, other):
        return True

    def __len__(self):
        raise ValueError('overridden len')

    def items(self):
        raise ValueError('overridden items')


assert PayloadDict(a=1) != {'b': 1}
assert dict.__eq__(PayloadDict(a=1), {'a': 1})
assert dict.__eq__({'a': 1}, PayloadDict(a=1))


class Value:
    def __eq__(self, other):
        raise ValueError('value equality')


value = Value()
assert {'a': value} == {'a': value}
try:
    {'a': value} == {'a': Value()}
    assert False
except ValueError as error:
    assert error.args == ('value equality',)


# Right-side key equality has priority during hash-table lookup.
trace = []


class LeftKey:
    def __hash__(self):
        return 42

    def __eq__(self, other):
        trace.append('left')
        return True


class RightKey:
    def __hash__(self):
        return 42

    def __eq__(self, other):
        trace.append('right')
        return True


assert {LeftKey(): 1} == {RightKey(): 1}
assert trace == ['right']


class Truth:
    def __bool__(self):
        return True


class EqualValue:
    def __eq__(self, other):
        return Truth()


assert {'a': EqualValue()} == {'a': EqualValue()}

# Builtin receiver checks and positional-only methods keep their call contract.
try:
    dict.__eq__([], {})
    assert False
except TypeError:
    pass
try:
    dict.__ne__([], {})
    assert False
except TypeError:
    pass
try:
    dict.__eq__(self={}, other={})
    assert False
except TypeError:
    pass
