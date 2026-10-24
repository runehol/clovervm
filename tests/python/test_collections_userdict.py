# Python test set -- collections.UserDict
#
# Independently written compatibility cases audited against CPython 3.14.5's
# Lib/collections/__init__.py and Lib/_collections_abc.py. No CPython test code
# is copied. The corresponding module adaptations retain PSF attribution.

from collections import UserDict


def raises(error_type, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error_type as error:
        return error
    assert False


# Public data is a fresh dict; construction copies sources and processes
# positional input before keywords. Positional-only names remain valid keys.
empty = UserDict()
assert empty.data.__class__ is dict
assert len(empty) == 0
assert not empty
assert UserDict(None) == {}
assert empty.data is not UserDict().data
source = {'a': 1, 'b': 2}
u = UserDict(source, a=3, self=4, dict=5)
assert u == {'a': 3, 'b': 2, 'self': 4, 'dict': 5}
assert source['a'] == 1
assert u.data is not source
assert UserDict(dict=1, self=2) == {'dict': 1, 'self': 2}
assert UserDict([('a', 1), ('a', 2)], a=3) == {'a': 3}
assert UserDict((('b', 4),)) == {'b': 4}
assert UserDict(UserDict(a=7)) == {'a': 7}


class KeysMapping:
    def keys(self):
        return ['b', 'a']

    def __getitem__(self, key):
        if key == 'a':
            return 10
        return 20


assert UserDict(KeysMapping(), a=30) == {'b': 20, 'a': 30}


class PairIterator:
    def __init__(self):
        self.index = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.index += 1
        if self.index == 1:
            return 'key'
        if self.index == 2:
            return 9
        raise StopIteration


assert UserDict([PairIterator()]) == {'key': 9}
# String pair elements need str.__iter__, which is not exposed by the VM.

# Item operations, public data replacement, iteration, and error payloads.
u = UserDict(a=1, b=2)
assert len(u) == 2 and u
assert u['a'] == 1
u['a'] = 3
u['c'] = 4
assert 'a' in u and 'missing' not in u
assert list(u) == ['a', 'b', 'c']
iterator = iter(u)
assert next(iterator) == 'a'
assert next(iterator) == 'b'
assert next(iterator) == 'c'
raises(StopIteration, next, iterator)
del u['b']
assert u == {'a': 3, 'c': 4}
assert raises(KeyError, UserDict.__getitem__, u, 'missing').args == ('missing',)
assert raises(KeyError, UserDict.__delitem__, u, 'missing').args == ('missing',)
# Reuse the expanded-call helper after missing-key errors to exercise warm
# membership caches when the needle changes from a string to an unhashable list.
raises(TypeError, UserDict.__getitem__, u, [])
raises(TypeError, UserDict.__setitem__, u, [], 1)
raises(TypeError, UserDict.__contains__, u, [])
u.data = {'replacement': 8}
assert list(u) == ['replacement'] and u['replacement'] == 8
assert repr(u) == repr(u.data)
assert repr(UserDict()) == '{}'

# Structural equality, reflected comparison, nonmapping fallback, and value
# identity before equality, matching dictionary equality's value comparison.
assert UserDict(a=1, b=2) == UserDict(b=2, a=1)
assert {'a': 1} == UserDict(a=1)
assert UserDict(a=1) != {'a': 2}
