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

assert UserDict(a=1) != {'b': 1}
assert UserDict(a=1) != {'a': 1, 'b': 2}
assert UserDict(a=1) != [('a', 1)]
assert UserDict().__eq__([]) is NotImplemented
assert UserDict().__ne__([]) is NotImplemented


class NeverEqual:
    def __eq__(self, other):
        return False


value = NeverEqual()
assert UserDict(a=value) == {'a': value}

# Views retain the wrapper, reflect mutation and data replacement, and return
# tuples for items. Values use identity before equality for membership.
u = UserDict(a=1, b=2)
keys = u.keys()
values = u.values()
items = u.items()
assert len(keys) == len(values) == len(items) == 2
assert list(keys) == ['a', 'b']
assert list(values) == [1, 2]
assert list(items) == [('a', 1), ('b', 2)]
assert next(iter(items)).__class__ is tuple
assert 'a' in keys and 'x' not in keys
assert 1 in values and 9 not in values
assert ('a', 1) in items and ('a', 2) not in items
assert ('x', 1) not in items
raises(ValueError, items.__class__.__contains__, items, ('a',))
raises(TypeError, items.__class__.__contains__, items, 1)
u['c'] = 3
assert len(keys) == 3 and 3 in values
u.data = {'z': value}
assert list(keys) == ['z'] and value in values and ('z', value) in items
assert repr(keys) == 'KeysView(' + repr(u) + ')'
assert repr(values) == 'ValuesView(' + repr(u) + ')'
assert repr(items) == 'ItemsView(' + repr(u) + ')'
