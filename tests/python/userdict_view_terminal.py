# Mapping-view wrappers own exhaustion and escaping-error state.
from collections import UserDict


def iterator_for(mapping, kind):
    if kind == 'keys':
        return iter(mapping.keys())
    if kind == 'values':
        return iter(mapping.values())
    return iter(mapping.items())


class Source:
    def __init__(self, failure):
        self.failure = failure
        self.calls = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.calls += 1
        if self.calls == 1:
            return 'a'
        if self.calls == 2:
            raise self.failure
        return 'resumed'


class Mapping(UserDict):
    def __iter__(self):
        return self.source

    def __getitem__(self, key):
        self.lookups += 1
        return self.data[key]


def assert_exhausted(iterator):
    for attempt in (1, 2):
        try:
            next(iterator)
            assert False
        except StopIteration as error:
            assert error.args == ()
            assert error.value is None


for kind in ('keys', 'values', 'items'):
    for failure in (StopIteration(123), ValueError('source'), BaseException('source')):
        mapping = Mapping(a=1)
        mapping.source = Source(failure)
        mapping.lookups = 0
        iterator = iterator_for(mapping, kind)
        assert iter(iterator) is iterator
        result = next(iterator)
        if kind == 'keys':
            assert result == 'a'
            assert mapping.lookups == 0
        elif kind == 'values':
            assert result == 1
        else:
            assert result == ('a', 1)
        if isinstance(failure, StopIteration):
            assert_exhausted(iterator)
        else:
            try:
                next(iterator)
                assert False
            except BaseException as error:
                assert error is failure
            assert_exhausted(iterator)
        assert mapping.source.calls == 2
        if kind == 'keys':
            assert mapping.lookups == 0
        else:
            assert mapping.lookups == 1
