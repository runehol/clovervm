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

# Subclass hooks are observable throughout the mixin paths.
class Scaled(UserDict):
    def __setitem__(self, key, value):
        self.data[key] = value + 1

    def __getitem__(self, key):
        return self.data[key] * 10

    def __delitem__(self, key):
        self.deleted = key
        del self.data[key]


s = Scaled(a=1)
assert s.data['a'] == 2 and s.get('a') == 20
assert list(s.values()) == [20] and list(s.items()) == [('a', 20)]
assert s == {'a': 20}
s.update([('b', 2)])
assert s.data['b'] == 3
assert s.setdefault('c', 4) == 4 and s.data['c'] == 5
assert s.pop('b') == 30 and s.deleted == 'b'
assert s.popitem() == ('a', 20) and s.deleted == 'a'
s.clear()
assert len(s) == 0 and s.deleted == 'c'


class IterationOverride(UserDict):
    def __iter__(self):
        return iter(['b'])

    def keys(self):
        return ['a']


# Registered UserDict mappings use __iter__, ahead of the keys protocol.
assert UserDict(IterationOverride(a=1, b=2)) == {'b': 2}

s = UserDict(a=0)
s.update(IterationOverride(a=1, b=2))
assert s == {'a': 0, 'b': 2}


class DictIterationOverride(dict):
    def __iter__(self):
        return iter(['b'])

    def keys(self):
        return ['a']


class InheritedDictIterationOverride(DictIterationOverride):
    pass


# Dict mappings also use supported class-level iteration ahead of keys().
for source in [DictIterationOverride(a=1, b=2),
               InheritedDictIterationOverride(a=1, b=2)]:
    assert UserDict(source) == {'b': 2}
    s = UserDict(a=0)
    assert s.update(source) is None
    assert s == {'a': 0, 'b': 2}

# Ordinary dicts retain the keys fallback while dict.__iter__ is unavailable.
assert UserDict({'a': 1, 'b': 2}) == {'a': 1, 'b': 2}
s = UserDict(a=0)
assert s.update({'a': 1, 'b': 2}) is None
assert s == {'a': 1, 'b': 2}


class KeysMapping:
    def __iter__(self):
        return iter(['b'])

    def keys(self):
        return ['a']

    def __getitem__(self, key):
        return 7


# Unregistered mappings still select keys(), even if they are iterable.
assert UserDict(KeysMapping()) == {'a': 7}
s = UserDict(b=0)
s.update(KeysMapping())
assert s == {'b': 0, 'a': 7}


class ItemsOverride(UserDict):
    def items(self):
        return [('virtual', 42)]


assert ItemsOverride(a=1) == {'virtual': 42}


class ContainsOverride(UserDict):
    def __contains__(self, key):
        return True

    def __getitem__(self, key):
        return 77


assert ContainsOverride().get('absent') == 77


class UpdateOverride(UserDict):
    def update(self, other=(), /, **kwds):
        self.calls = getattr(self, 'calls', 0) + 1
        UserDict.update(self, other, **kwds)


s = UpdateOverride({'a': 1}, b=2)
assert s.calls == 2 and s == {'a': 1, 'b': 2}


class PopitemOverride(UserDict):
    def popitem(self):
        self.called = True
        return UserDict.popitem(self)


s = PopitemOverride(a=1)
s.clear()
assert s.called and len(s) == 0

# Missing handlers are looked up on the class, including inheritance. get and
# membership bypass them; setdefault accepts their result without inserting;
# pop then attempts deletion and can consequently raise KeyError.
class Missing(UserDict):
    def __missing__(self, key):
        return 99


class InheritedMissing(Missing):
    pass


s = InheritedMissing()
assert s['absent'] == 99 and 'absent' not in s
assert s.get('absent', 7) == 7
assert s.setdefault('absent', 7) == 99 and len(s) == 0
assert raises(KeyError, UserDict.pop, s, 'absent', 7).args == ('absent',)
assert ('absent', 99) in s.items()
u = UserDict()
u.__missing__ = 1234
assert raises(KeyError, UserDict.__getitem__, u, 'absent').args == ('absent',)

# A missing handler can insert and return a value; get still does not call it.
class InsertingMissing(UserDict):
    def __missing__(self, key):
        self[key] = 5
        return 5


s = InsertingMissing()
assert s.get('a') is None and len(s) == 0
assert s.pop('a') == 5 and len(s) == 0
assert s.setdefault('a', 9) == 5 and s.data['a'] == 5
assert 'anything' in ContainsOverride().keys()
assert list(IterationOverride(a=1, b=2).values()) == [2]
