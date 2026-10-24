# Ordinary string iteration, cross-checked with CPython 3.14.5.

def raises(error, function, *args):
    try:
        function(*args)
    except error:
        return
    raise AssertionError('expected exception')


cases = [
    ('', []),
    ('abc', ['a', 'b', 'c']),
    ('a\u00e9\U0001f642\ud800\udfff\x00',
     ['a', '\u00e9', '\U0001f642', '\ud800', '\udfff', '\x00']),
]
for case in cases:
    source = case[0]
    expected = case[1]
    iterator = iter(source)
    assert iter(iterator) is iterator
    for character in expected:
        assert next(iterator) == character
    for unused in range(3):
        try:
            next(iterator)
        except StopIteration as exhausted:
            assert exhausted.args == ()
        else:
            assert False
        assert next(iterator, 42) == 42
    assert list(iterator) == []
    assert list(source) == expected
    collected = []
    for character in source:
        collected.append(character)
    assert collected == expected

first = iter('ab')
second = iter('ab')
assert first is not second
assert next(first) == 'a'
assert next(first) == 'b'
assert next(second) == 'a'
assert next(second) == 'b'


class Inherited(str):
    def __len__(self):
        raise AssertionError('length hook called')

    def __getitem__(self, index):
        raise AssertionError('index hook called')


# Wrap in iter before list to avoid the consumer's length hint.
source = Inherited('a\U0001f642\ud800')
assert list(iter(source)) == ['a', '\U0001f642', '\ud800']
assert list(str.__iter__(source)) == ['a', '\U0001f642', '\ud800']
assert next(iter(source)).__class__ is str
collected = []
for character in source:
    collected.append(character)
assert collected == ['a', '\U0001f642', '\ud800']


class Custom(str):
    def __iter__(self):
        return iter(('custom', 'iterator'))


assert list(Custom('payload')) == ['custom', 'iterator']
assert list(str.__iter__(Custom('payload'))) == list('payload')

error = ValueError('iteration failed')


class Broken(str):
    def __iter__(self):
        raise error


try:
    list(Broken('payload'))
except ValueError as caught:
    assert caught is error
else:
    assert False


class BrokenNext:
    def __iter__(self):
        return self

    def __next__(self):
        raise error


class CustomBrokenNext(str):
    def __iter__(self):
        return BrokenNext()


try:
    list(CustomBrokenNext('payload'))
except ValueError as caught:
    assert caught is error
else:
    assert False

raises(TypeError, str.__iter__, 1)
iterator_class = iter('').__class__
raises(TypeError, iterator_class.__iter__, 'x')
raises(TypeError, iterator_class.__next__, 'x')
# Errors and exhaustion leave the next independent iteration usable.
assert list('ok') == ['o', 'k']

# Consumers used by stdlib code; bounded islice must honor subclass iteration.
from itertools import chain, islice
assert list(chain('ab', '\U0001f642')) == ['a', 'b', '\U0001f642']
assert list(islice(Custom('ignored'), 1)) == ['custom']
assert ''.join(list(iter('a\ud800\U0001f642'))) == 'a\ud800\U0001f642'

# The iterator owns a dynamically produced string after its local disappears.
def make_iterator():
    value = 'payload-' + str(12345)
    return iter(value)

assert ''.join(list(make_iterator())) == 'payload-12345'
