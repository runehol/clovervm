# Contract probes checked against CPython 3.14.10 (Objects/stringlib/join.h).
# This file intentionally avoids the documented length-hint/buffer limitations.

class B(bytes):
    def __bytes__(self):
        raise AssertionError('conversion hook')
    def __len__(self):
        raise AssertionError('length hook')
    def __getitem__(self, index):
        raise AssertionError('index hook')
    def __iter__(self):
        raise AssertionError('iteration hook')

for separator in (b'', b':', b'\x00\xff', B(b'--')):
    for sequence in ([], (), [b''], (bytearray(),), [B(b'')]):
        result = bytes.join(separator, sequence)
        assert result == b''
        assert result.__class__ is bytes
    for element in (b'payload', B(b'payload'), bytearray(b'payload')):
        result = separator.join([element])
        assert result == b'payload'
        assert result.__class__ is bytes
        if element.__class__ is bytes:
            assert result is element
        else:
            assert result is not element

assert b':'.join([b'a', bytearray(b'b'), B(b'c')]) == b'a:b:c'
assert b'\x00'.join((b'\xff', b'', b'\x80')) == b'\xff\x00\x00\x80'
assert b','.join({b'a': 1}) == b'a'
assert b'-'.join(iter([b'a', b'b'])) == b'a-b'

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

source = Source([b'a', bytearray(b'b')])
assert b'-'.join(source) == b'a-b'
assert source.events == ['iter', 'iter', 'next', 'next', 'next']

class ChangedIterator(Source):
    def __iter__(self):
        self.events.append('iter')
        if len(self.events) == 2:
            return iter([b'changed'])
        return self

source = ChangedIterator([b'original'])
assert b'-'.join(source) == b'changed'
assert source.events == ['iter', 'iter']

class L(list):
    def __iter__(self):
        return iter([b'override'])
    def __len__(self):
        raise AssertionError('sequence length hook')
    def __getitem__(self, key):
        raise AssertionError('sequence item hook')

class T(tuple):
    def __iter__(self):
        return iter([b'override'])
    def __len__(self):
        raise AssertionError('sequence length hook')
    def __getitem__(self, key):
        raise AssertionError('sequence item hook')

assert b'-'.join(L([b'original'])) == b'override'
assert b'-'.join(T((b'original',))) == b'override'

# Source callbacks can resize, replace, and remove previously yielded values.
class Mutating(Source):
    def __next__(self):
        if self.index == 0:
            self.index = 1
            return self.values[0]
        if self.index == 1:
            self.values[0][:] = b'changed'
            self.index = 2
            return self.values[0]
        self.values[0].extend(b'!')
        self.values.clear()
        raise StopIteration

mutable = bytearray(b'old')
assert b'-'.join(Mutating([mutable])) == b'changed!-changed!'
assert mutable == bytearray(b'changed!')

class Reentrant(Source):
    def __next__(self):
        assert b'/'.join([B(b'inner'), bytearray(b'join')]) == b'inner/join'
        return Source.__next__(self)

for index in range(40):
    assert B(b':').join(Reentrant([bytearray(b'a'), B(b'b')])) == b'a:b'

class LateError(Source):
    def __next__(self):
        if self.index == 0:
            self.index = 1
            return None
        raise RuntimeError('late iterator failure')

try:
    b'-'.join(LateError([]))
    assert False
except RuntimeError as error:
    assert str(error) == 'late iterator failure'

source = Source([b'a', None, b'b'])
try:
    b'-'.join(source)
    assert False
except TypeError as error:
    assert str(error) == 'sequence item 1: expected a bytes-like object, NoneType found'
assert source.events == ['iter', 'iter', 'next', 'next', 'next', 'next']

for invalid in (None, 1, True, 'a', [1], (b'a',)):
    try:
        b'-'.join([b'valid', invalid])
        assert False
    except TypeError:
        pass
for invalid in (1, None):
    try:
        b'-'.join(invalid)
        assert False
    except TypeError as error:
        assert str(error) == 'can only join an iterable'
for invalid in (b'a', bytearray(b'a'), 'a'):
    try:
        b'-'.join(invalid)
        assert False
    except TypeError:
        pass

# Binding/receiver rejection must precede source callbacks.
source = Source([b'a'])
for receiver in ('-', bytearray(b'-'), 1):
    try:
        bytes.join(receiver, source)
        assert False
    except TypeError:
        pass
assert source.events == []
try:
    b'-'.join(iterable=source)
    assert False
except TypeError:
    pass
try:
    b'-'.join()
    assert False
except TypeError:
    pass
try:
    b'-'.join(source, source)
    assert False
except TypeError:
    pass
assert source.events == []

# Retained native payloads and method calls continue to work after warm-up.
parts = []
expected = bytearray()
for index in range(256):
    parts.append(bytearray([index]))
    expected.append(index)
for index in range(20):
    result = b''.join(parts)
    assert result == bytes(expected)
    assert result.__class__ is bytes
parts[0][0] = 42
assert result[0] == 0

class FirstIterError:
    def __iter__(self):
        raise TypeError('first acquisition failure')

try:
    b'-'.join(FirstIterError())
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
    b'-'.join(SecondIterError([]))
    assert False
except TypeError as error:
    assert str(error) == 'second acquisition failure'

# Warm inherited method lookup, override it, then restore its implementation.
class Separator(bytes):
    pass

def call_join(separator, values):
    return separator.join(values)

separator = Separator(b':')
for index in range(30):
    assert call_join(separator, [b'a', b'b']) == b'a:b'

def replacement(self, values):
    return b'override'

Separator.join = replacement
assert call_join(separator, [b'a', b'b']) == b'override'
Separator.join = bytes.join
assert call_join(separator, [b'a', b'b']) == b'a:b'
