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

# Mutation methods return None or the appropriate stored/default value.
u = UserDict(a=1)
assert u.get('a') == 1 and u.get('x') is None
assert u.get(key='x', default=9) == 9
assert u.update({'a': 2}, a=3, self=4, other=5, kwds=6) is None
assert u == {'a': 3, 'self': 4, 'other': 5, 'kwds': 6}
assert u.update() is None
assert u.update([('a', 7), ('a', 8)], a=9) is None
assert u['a'] == 9
assert u.update(u) is None
assert u.setdefault(key='a', default=99) == 9
assert u.setdefault('new') is None and 'new' in u
assert u.setdefault('value', 10) == 10
assert u.pop(key='value', default=99) == 10
assert u.pop('missing', None) is None
assert raises(KeyError, UserDict.pop, u, 'missing').args == ('missing',)
assert u.pop('new') is None and 'new' not in u
u = UserDict(a=1, b=2)
assert u.popitem() == ('a', 1)
assert u.popitem() == ('b', 2)
assert raises(KeyError, UserDict.popitem, u).args == ()
u.update(a=1, b=2)
assert u.clear() is None and len(u) == 0
assert u.clear() is None

# Update is incremental; failure leaves prior writes, and keywords are applied
# only after the positional source has been consumed successfully.
u = UserDict()
raises(ValueError, UserDict.update, u, [('ok', 1), ('bad',)], later=2)
assert u == {'ok': 1}
raises(ValueError, UserDict.update, u, [('a', 1, 2)])
raises(TypeError, UserDict.update, u, [1])
raises(TypeError, UserDict.update, u, None)
raises(TypeError, UserDict.update, u, [([], 1)])
raises(TypeError, UserDict, {}, {})
raises(TypeError, UserDict.update, u, {}, {})
raises(TypeError, UserDict.get, u)
raises(TypeError, UserDict.get, u, 'a', 1, 2)
raises(TypeError, UserDict.get, u, 'a', unknown=1)
raises(TypeError, UserDict.setdefault, u)
raises(TypeError, UserDict.pop, u)
raises(TypeError, UserDict.pop, u, 'a', 1, 2)
raises(TypeError, UserDict.popitem, u, 1)
raises(TypeError, UserDict.clear, u, extra=1)
raises(TypeError, UserDict.copy, u, 1)
raises(TypeError, UserDict.keys, u, 1)
raises(TypeError, UserDict.values, u, extra=1)
raises(TypeError, UserDict.items, u, 1)

# Base copies preserve shallow value identity and own an independent dict.
value = []
u = UserDict(a=value)
c = u.copy()
assert c.__class__ is UserDict and c is not u and c.data is not u.data
assert c['a'] is value
c['b'] = 2
assert 'b' not in u


# Exceptions from mapping sources propagate after prior writes, and do not
# apply keyword updates after failure.
failure = ValueError('source failed')


class FailingMapping:
    def keys(self):
        return ['ok', 'bad']

    def __getitem__(self, key):
        if key == 'bad':
            raise failure
        return 42


u = UserDict()
assert raises(ValueError, UserDict.update, u, FailingMapping(), later=2) is failure
assert u == {'ok': 42}
