# Cross-checked with CPython 3.14.10; length hints are tested separately.
class S(str):
    def __str__(self):
        raise AssertionError('conversion hook')
    def __len__(self):
        raise AssertionError('length hook')
    def __getitem__(self, index):
        raise AssertionError('index hook')
    def __iter__(self):
        raise AssertionError('iteration hook')

for separator in ('', ':', '\x00\U0001f642', S('--')):
    for sequence in ([], (), [''], (S(''),)):
        result = str.join(separator, sequence)
        assert result == ''
        assert result.__class__ is str
    for element in ('payload', S('payload')):
        result = separator.join([element])
        assert result == 'payload'
        assert result.__class__ is str
        if element.__class__ is str:
            assert result is element
        else:
            assert result is not element

assert ':'.join(['a', S('b'), 'c']) == 'a:b:c'
assert '\x00'.join(('\ud800', '', '\U0001f642')) == '\ud800\x00\x00\U0001f642'
assert '-'.join('ab') == 'a-b'
assert ','.join({'a': 1}) == 'a'
assert '-'.join(iter(['a', 'b'])) == 'a-b'
assert '-'.join(bytearray()) == ''
try:
    '-'.join(bytearray(b'x'))
    assert False
except TypeError as error:
    assert str(error) == 'sequence item 0: expected str instance, int found'

class Source:
    def __init__(self, values):
        self.values = values
        self.index = 0
        self.events = []
    def __iter__(self):
        self.events.append('iter')
        return self
    def __next__(self):
        self.events.append('next')
        if self.index == len(self.values):
            raise StopIteration
        result = self.values[self.index]
        self.index += 1
        return result

source = Source(['a', 'b'])
assert '-'.join(source) == 'a-b'
assert source.events == ['iter', 'iter', 'next', 'next', 'next']

class ChangedIterator(Source):
    def __iter__(self):
        self.events.append('iter')
        if len(self.events) == 2:
            return iter(['changed'])
        return self
source = ChangedIterator(['original'])
assert '-'.join(source) == 'changed'
assert source.events == ['iter', 'iter']

class L(list):
    def __iter__(self):
        return iter(['override'])
    def __len__(self):
        raise AssertionError('sequence length hook')
    def __getitem__(self, key):
        raise AssertionError('sequence item hook')
class T(tuple):
    def __iter__(self):
        return iter(['override'])
    def __len__(self):
        raise AssertionError('sequence length hook')
    def __getitem__(self, key):
        raise AssertionError('sequence item hook')
assert '-'.join(L(['original'])) == 'override'
assert '-'.join(T(('original',))) == 'override'

# The private snapshot retains elements removed from their original source.
class Mutating(Source):
    def __next__(self):
        if self.index == 0:
            self.index = 1
            return self.values[0]
        self.values.clear()
        raise StopIteration
assert '-'.join(Mutating([S('retained payload')])) == 'retained payload'

class Reentrant(Source):
    def __next__(self):
        assert '/'.join([S('inner'), 'join']) == 'inner/join'
        return Source.__next__(self)
for index in range(40):
    assert S(':').join(Reentrant([S('a'), 'b'])) == 'a:b'

class LateError(Source):
    def __next__(self):
        if self.index == 0:
            self.index = 1
            return None
        raise RuntimeError('late iterator failure')
try:
    '-'.join(LateError([]))
    assert False
except RuntimeError as error:
    assert str(error) == 'late iterator failure'
source = Source(['a', None, 'b'])
try:
    '-'.join(source)
    assert False
except TypeError as error:
    assert str(error) == 'sequence item 1: expected str instance, NoneType found'
assert source.events == ['iter', 'iter', 'next', 'next', 'next', 'next']

# A source TypeError during consumption is not rewritten as acquisition failure.
class NextTypeError(Source):
    def __next__(self):
        raise TypeError('next failure')
try:
    '-'.join(NextTypeError([]))
    assert False
except TypeError as error:
    assert str(error) == 'next failure'

source_error = ValueError('source failure')
class IterValueError:
    def __iter__(self):
        raise source_error
try:
    '-'.join(IterValueError())
    assert False
except ValueError as error:
    assert error is source_error

class FirstIterError:
    def __iter__(self):
        raise TypeError('first acquisition failure')
try:
    '-'.join(FirstIterError())
    assert False
except TypeError as error:
    assert str(error) == 'can only join an iterable'
class SecondIterError(Source):
    def __iter__(self):
        self.events.append('iter')
        if len(self.events) == 2:
            raise TypeError('second acquisition failure')
        return self
try:
    '-'.join(SecondIterError([]))
    assert False
except TypeError as error:
    assert str(error) == 'second acquisition failure'

source = Source(['a'])
for receiver in (b'-', 1, None):
    try:
        str.join(receiver, source)
        assert False
    except TypeError:
        pass
try:
    '-'.join(iterable=source)
    assert False
except TypeError:
    pass
try:
    '-'.join()
    assert False
except TypeError:
    pass
try:
    '-'.join(source, source)
    assert False
except TypeError:
    pass
assert source.events == []
for invalid in (1, None):
    try:
        '-'.join(invalid)
        assert False
    except TypeError as error:
        assert str(error) == 'can only join an iterable'

class Separator(str):
    pass
def call_join(separator, values):
    return separator.join(values)
separator = Separator(':')
for index in range(30):
    assert call_join(separator, ['a', 'b']) == 'a:b'
def replacement(self, values):
    return 'override'
Separator.join = replacement
assert call_join(separator, ['a', 'b']) == 'override'
Separator.join = str.join
assert call_join(separator, ['a', 'b']) == 'a:b'

parts = []
for index in range(256):
    parts.append(S('payload' + str(index)))
for index in range(20):
    result = ':'.join(parts)
    assert result.__class__ is str
    assert result.startswith('payload0:payload1:')
    assert result.endswith(':payload255')
