# Missing-key errors retain the original key without an extra lookup/hash.
from collections import UserDict


def expect_missing(function, mapping, key):
    try:
        function(mapping, key)
        assert False
    except KeyError as error:
        assert len(error.args) == 1 and error.args[0] is key


def read(mapping, key):
    return mapping[key]


def delete(mapping, key):
    del mapping[key]


# Warm string handlers, then exercise their missing-key paths. General-key
# generated methods and explicit unbound methods must carry the same payload.
for key in ('absent', 42):
    expect_missing(read, {}, key)
    expect_missing(delete, {}, key)
    expect_missing(dict.__getitem__, {}, key)
    expect_missing(dict.__delitem__, {}, key)
    expect_missing(dict.pop, {}, key)
    expect_missing(UserDict.__delitem__, UserDict(), key)

assert read({'warm': 1}, 'warm') == 1
expect_missing(read, {}, 'missing')
delete({'warm': 1}, 'warm')
expect_missing(delete, {}, 'missing')
try:
    UserDict().popitem()
    assert False
except KeyError as error:
    assert len(error.args) == 0


class Key:
    def __init__(self):
        self.calls = 0

    def __hash__(self):
        self.calls += 1
        if self.calls == 2:
            raise ValueError('hashed twice')
        return 42


key = Key()
u = UserDict()
u.data[key] = 1
key.calls = 0
del u[key]
assert len(u) == 0 and key.calls == 1

failure = KeyError('callback')


class FailingKey:
    def __hash__(self):
        raise failure


try:
    del UserDict()[FailingKey()]
    assert False
except KeyError as error:
    assert error is failure
