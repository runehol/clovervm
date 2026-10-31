# Also runs unchanged on CPython.
assert list(b'') == []
assert list(b'\x00\x7f\x80\xff') == [0, 127, 128, 255]
assert tuple(b'ab') == (97, 98)
assert b''.join(b'') == b''
try:
    b''.join(b'x')
    assert False
except TypeError:
    pass
it = iter(b'ab')
assert iter(it) is it
assert next(it) == 97
other = iter(b'ab')
assert next(other) == 97
assert next(it) == 98
assert next(it, 'done') == 'done'
assert next(it, 'done') == 'done'
assert next(other) == 98
assert next(iter(b'\xff')) == 255
out = []
for item in b'\x00\xff':
    out.append(item)
assert out == [0, 255]
class Stored(bytes):
    def __getitem__(self, key):
        raise AssertionError('index override must not run')
    def __len__(self):
        raise AssertionError('length override must not run')
assert list(iter(Stored(b'\x00\xff'))) == [0, 255]
assert list(bytes.__iter__(Stored(b'ab'))) == [97, 98]
class Custom(Stored):
    def __iter__(self):
        return iter([9])
assert list(iter(Custom(b'ab'))) == [9]
assert list(bytes.__iter__(Custom(b'ab'))) == [97, 98]
for bad in (None, 1, 'ab', bytearray(b'ab')):
    try:
        bytes.__iter__(bad)
        assert False
    except TypeError:
        pass
# Exercise repeated acquisition and native method call caches with both layouts.
def consume(value):
    return list(value)
for i in range(50):
    assert consume(b'\x80\xff') == [128, 255]
    assert consume(bytearray(b'ab')) == [97, 98]
all_values = list(range(256))
assert list(bytes(all_values)) == all_values
# Keep only the iterator as an owner while loop bodies allocate and reenter.
def make_iterator():
    return iter(Stored(b'\x00\x80\xff'))
it = make_iterator()
for expected in (0, 128, 255):
    scratch = []
    for i in range(20):
        scratch.append(str(i))
    assert next(it) == expected
assert next(it, None) is None
assert next(it, None) is None
# Acquisition must continue to observe class-level iterator replacements.
class Changing(bytes):
    pass
assert consume(Changing(b'a')) == [97]
def alternate(self):
    return iter([42])
Changing.__iter__ = alternate
assert consume(Changing(b'a')) == [42]
del Changing.__iter__
assert consume(Changing(b'a')) == [97]
iterator_class = iter(b'').__class__
try:
    iterator_class()
    assert False
except TypeError:
    pass
try:
    class InvalidIteratorSubclass(iterator_class):
        pass
    assert False
except TypeError:
    pass
