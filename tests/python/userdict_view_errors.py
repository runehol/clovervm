# View iterator errors are terminal, matching CPython 3.14.5's mapping views.
from collections import UserDict


def iterator_for(mapping, kind):
    if kind == 'values':
        return iter(mapping.values())
    return iter(mapping.items())


class FailingLookup(UserDict):
    def __getitem__(self, key):
        self.calls += 1
        if key == 'a':
            raise self.failure
        return self.data[key]


for kind in ('values', 'items'):
    for failure in (ValueError('lookup'), KeyError('lookup'), BaseException('lookup')):
        mapping = FailingLookup(a=1, b=2)
        mapping.calls = 0
        mapping.failure = failure
        iterator = iterator_for(mapping, kind)
        try:
            next(iterator)
            assert False
        except BaseException as error:
            assert error is failure
        assert next(iterator, 'exhausted') == 'exhausted'
        assert next(iterator, 'exhausted') == 'exhausted'
        assert mapping.calls == 1

    mapping = FailingLookup(a=1, b=2)
    mapping.calls = 0
    mapping.failure = StopIteration('from lookup')
    iterator = iterator_for(mapping, kind)
    try:
        next(iterator)
        assert False
    except RuntimeError as error:
        assert error.args == ('generator raised StopIteration',)
    assert next(iterator, 'exhausted') == 'exhausted'
    assert mapping.calls == 1
    assert next(iterator_for(UserDict(), kind), 'exhausted') == 'exhausted'


class FailingKeys:
    def __init__(self):
        self.calls = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.calls += 1
        raise ValueError('from keys')


class KeyFailure(UserDict):
    def __iter__(self):
        return self.keys_iterator


for kind in ('values', 'items'):
    mapping = KeyFailure(a=1)
    mapping.keys_iterator = FailingKeys()
    iterator = iterator_for(mapping, kind)
    try:
        next(iterator)
        assert False
    except ValueError as error:
        assert error.args == ('from keys',)
    assert next(iterator, 'exhausted') == 'exhausted'
    assert mapping.keys_iterator.calls == 1


class Reenter(UserDict):
    def __getitem__(self, key):
        try:
            next(self.iterator)
            assert False
        except ValueError as error:
            assert error.args == ('generator already executing',)
        return self.data[key]


for kind in ('values', 'items'):
    mapping = Reenter(a=1)
    mapping.iterator = iterator_for(mapping, kind)
    result = next(mapping.iterator)
    if kind == 'values':
        assert result == 1
    else:
        assert result == ('a', 1)
    assert next(mapping.iterator, 'exhausted') == 'exhausted'
