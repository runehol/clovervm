# Independently written dict iteration dispatch cases checked against CPython 3.14.5.


class KeysOverride(dict):
    def keys(self):
        raise AssertionError('dict iteration must bypass keys')


class Inherited(KeysOverride):
    pass


class Override(dict):
    def __iter__(self):
        return iter(['override'])


def collect(source):
    result = []
    for key in source:
        result.append(key)
    return result


for count in range(12):
    assert collect(KeysOverride(a=1, b=2)) == ['a', 'b']
    assert collect(Inherited(a=1)) == ['a']
    assert collect(Override(a=1)) == ['override']
    assert list(dict.__iter__(Override(a=1))) == ['a']


class Mutable(dict):
    pass


def replacement(self):
    return iter(['replacement'])


source = Mutable(a=1)
for count in range(12):
    assert collect(source) == ['a']
Mutable.__iter__ = replacement
assert collect(source) == ['replacement']
del Mutable.__iter__
assert collect(source) == ['a']


def failing_iter(self):
    raise ValueError('iter failure')


Mutable.__iter__ = failing_iter
try:
    collect(source)
    assert False
except ValueError as error:
    assert error.args == ('iter failure',)
del Mutable.__iter__
assert collect(source) == ['a']


class FailingNext:
    def __init__(self):
        self.count = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.count += 1
        if self.count < 12:
            return 'key'
        raise ValueError('next failure')


class NextOverride(dict):
    def __iter__(self):
        return FailingNext()


try:
    collect(NextOverride())
    assert False
except ValueError as error:
    assert error.args == ('next failure',)
