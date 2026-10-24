from collections import UserDict


class ReenteringSource:
    def __init__(self, catch):
        self.catch = catch
        self.calls = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.calls += 1
        if self.calls > 1:
            raise StopIteration
        if self.catch:
            try:
                next(self.outer)
                assert False
            except ValueError as error:
                assert error.args == ('generator already executing',)
        else:
            next(self.outer)
        return 'a'


class Mapping(UserDict):
    def __iter__(self):
        return self.source

    def __getitem__(self, key):
        raise AssertionError('keys iteration must not look up values')


for catch in (True, False):
    mapping = Mapping(a=1)
    source = ReenteringSource(catch)
    mapping.source = source
    iterator = iter(mapping.keys())
    source.outer = iterator
    if catch:
        assert next(iterator) == 'a'
    else:
        try:
            next(iterator)
            assert False
        except ValueError as error:
            assert error.args == ('generator already executing',)
    assert next(iterator, 'exhausted') == 'exhausted'
    assert next(iterator, 'exhausted') == 'exhausted'
    if catch:
        assert source.calls == 2
    else:
        assert source.calls == 1
