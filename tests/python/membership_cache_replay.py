# Membership replay must retain the operand shapes proved by the resolver.
# These cases are independently written and also runnable against CPython.


def contains(mapping, key):
    return key in mapping


def expanded(function, *args, **kwargs):
    return function(*args, **kwargs)


# A warm exact-string dict handler must not replay for an unhashable key.
assert not contains({}, 'warm')
try:
    contains({}, [])
    assert False
except TypeError:
    pass

# Preserve the original failure path through expanded calls as well.
assert not expanded(contains, {}, 'warm')
try:
    expanded(contains, {}, [])
    assert False
except TypeError:
    pass

# Valid keys of another shape must take the normal general-key path too.
assert not contains({}, 'warm')
assert not contains({}, 1)
assert contains({1: 'present'}, 1)
assert contains({'present': 1}, 'present')
assert not contains({}, 'absent')


class Key:
    def __init__(self):
        self.hash_calls = 0
        self.equality_calls = 0

    def __hash__(self):
        self.hash_calls += 1
        # Nested execution must preserve the outer membership frame/cache.
        assert expanded(contains, {'nested': 1}, 'nested')
        return 'stored'.__hash__()

    def __eq__(self, other):
        self.equality_calls += 1
        return other == 'stored'


assert not contains({}, 'warm')
key = Key()
assert expanded(contains, {'stored': 1}, key)
assert key.hash_calls == 1 and key.equality_calls == 1

failure = ValueError('hash failed')


class FailingKey:
    def __hash__(self):
        raise failure


assert not contains({}, 'warm')
try:
    expanded(contains, {}, FailingKey())
    assert False
except ValueError as error:
    assert error is failure
assert contains({'after': 1}, 'after')


# Membership guards only the container's method lookup validity. Class method
# replacement still invalidates a warm entry, including after a needle change.
class Container:
    def __contains__(self, key):
        return key == 'yes'


container = Container()
assert contains(container, 'yes')
assert not contains(container, 1)


def always(self, key):
    return True


Container.__contains__ = always
assert contains(container, 1)

# Existing string and bytes handlers accept their supported needle forms and
# still raise the expected errors after warming with another needle shape.
assert contains('abc', 'a')
try:
    contains('abc', 1)
    assert False
except TypeError:
    pass
assert contains(b'abc', b'a')
assert contains(b'abc', 97)
try:
    contains(b'abc', [])
    assert False
except TypeError:
    pass
