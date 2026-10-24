# Independently written dict iteration regressions checked against CPython 3.14.5.


def raises(error_type, function, *args):
    try:
        function(*args)
    except error_type as error:
        return error
    assert False


def retained_source():
    return iter({'alive': 1})


assert next(retained_source()) == 'alive'
assert list({}) == []
assert list({'b': 2, 'a': 1, 3: 'three'}) == ['b', 'a', 3]
assert list(dict.__iter__({'a': 1})) == ['a']
raises(TypeError, dict.__iter__, [])
raises(TypeError, dict.__iter__)
raises(TypeError, dict.__iter__, {}, 1)

# Independent iterators share the live source, not a cursor or a snapshot.
d = {'a': 1, 'b': 2, 'c': 3}
del d['b']
d['b'] = 4
i = iter(d)
j = iter(d)
assert i is not j and iter(i) is i
assert next(i) == 'a' and next(j) == 'a'
d['c'] = 5
assert next(i) == 'c' and next(i) == 'b'
assert list(j) == ['c', 'b']
error = raises(StopIteration, next, i)
assert error.args == () and error.value is None
assert next(i, 'done') == 'done'

# Promotion caused by a non-string lookup preserves cursor/order and size.
d = {'a': 1, 'b': 2}
i = iter(d)
assert next(i) == 'a'
assert d.get(1) is None
assert next(i) == 'b'
assert next(i, 'done') == 'done'

# Size changes are checked on next(), and detected failures stay sticky.
for operation in ('insert', 'delete', 'clear'):
    d = {'a': 1, 'b': 2}
    i = iter(d)
    assert next(i) == 'a'
    if operation == 'insert':
        d['c'] = 3
    elif operation == 'delete':
        del d['b']
    else:
        d.clear()
    error = raises(RuntimeError, next, i)
    assert str(error) == 'dictionary changed size during iteration'
    d.clear()
    d['a'] = 1
    d['b'] = 2
    raises(RuntimeError, next, i)

# A generic for-loop catches only exhaustion, not mutation errors.
d = {'a': 1}
try:
    for key in d:
        d['b'] = 2
    assert False
except RuntimeError:
    pass
