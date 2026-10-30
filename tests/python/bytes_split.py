# Portions adapted from CPython Lib/test/string_tests.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

for method in (bytes.split, bytes.rsplit):
    assert method(b'') == []
    assert method(b'', b':') == [b'']
    assert method(b' \t\n\r\v\f ') == []
    assert method(b'a:b::c:', b':') == [b'a', b'b', b'', b'c', b'']
    assert method(b'a//b//c', b'//') == [b'a', b'b', b'c']
    assert method(b'a\x00b\x00c', b'\x00') == [b'a', b'b', b'c']
    assert method(b'\x1c\x1d\x1e\x1f\x85\xa0') == [b'\x1c\x1d\x1e\x1f\x85\xa0']
    assert method(b'a b c', None, -2) == [b'a', b'b', b'c']
    assert method(b'a b c', None, -9223372036854775808) == [b'a', b'b', b'c']
    assert method(b'a:b:c', b':', 9223372036854775807) == [b'a', b'b', b'c']
    assert method(b'a:b:c', b':', 288230376151711744) == [b'a', b'b', b'c']
    assert method(b'a:b:c', b':', False) == [b'a:b:c']
    assert method(b'abc', b'longer') == [b'abc']
    assert method(b'abc', sep=b'b', maxsplit=1) == [b'a', b'c']
    for sep in (b'', bytearray()):
        for count in (0, -1, 1):
            raised = False
            try:
                method(b'', sep, count)
            except ValueError:
                raised = True
            assert raised
    for count in (9223372036854775808, -9223372036854775809, 10**100):
        raised = False
        try:
            method(b'a', None, count)
        except OverflowError:
            raised = True
        assert raised
    for count in (None, '1', 1.0, []):
        raised = False
        try:
            method(b'a', b'', count)
        except TypeError:
            raised = True
        assert raised
    for sep in (1, True, 'a', [], (b'a',)):
        raised = False
        try:
            method(b'a', sep)
        except TypeError:
            raised = True
        assert raised

assert b'  a b  '.split(None, 0) == [b'a b  ']
assert b'  a b  '.rsplit(None, 0) == [b'  a b']
assert b'  a b  '.split(None, 1) == [b'a', b'b  ']
assert b'  a b  '.rsplit(None, 1) == [b'  a', b'b']
assert b'  a b  '.split(None, 2) == [b'a', b'b']
assert b'  a b  '.rsplit(None, 2) == [b'a', b'b']
assert b'ababa'.split(b'aba') == [b'', b'ba']
assert b'ababa'.rsplit(b'aba') == [b'ab', b'']
assert b'a:b:c'.split(b':', True) == [b'a', b'b:c']
assert b'a:b:c'.rsplit(b':', True) == [b'a:b', b'c']

class B(bytes):
    def __bytes__(self):
        raise AssertionError
    def __len__(self):
        raise AssertionError
    def __getitem__(self, key):
        raise AssertionError
    def __iter__(self):
        raise AssertionError
    def split(self, *args):
        raise AssertionError
    def find(self, *args):
        raise AssertionError

class I(int):
    def __index__(self):
        raise AssertionError
    def __int__(self):
        raise AssertionError

source = B(b'a:b:c')
for method in (bytes.split, bytes.rsplit):
    for sep in (b':', B(b':'), bytearray(b':')):
        parts = method(source, sep, I(3))
        assert parts == [b'a', b'b', b'c']
        assert parts.__class__ is list
        for part in parts:
            assert part.__class__ is bytes
    assert method(B(b'abc'), b':')[0].__class__ is bytes
    assert method(B(b''), b':')[0].__class__ is bytes
    assert method(B(b'abc'))[0].__class__ is bytes
    assert method(B(b'abc'), b':', 0)[0].__class__ is bytes

for method in (bytes.partition, bytes.rpartition):
    for sep in (b':', B(b':'), bytearray(b':')):
        result = method(source, sep)
        assert result.__class__ is tuple
        assert len(result) == 3
        assert result[1] is sep
        assert result[0].__class__ is bytes
        assert result[2].__class__ is bytes
    for sep in (b'', bytearray()):
        raised = False
        try:
            method(b'', sep)
        except ValueError:
            raised = True
        assert raised
    for sep in (None, 1, True, ':', []):
        raised = False
        try:
            method(b'a', sep)
        except TypeError:
            raised = True
        assert raised
    raised = False
    try:
        method(b'a', sep=b':')
    except TypeError:
        raised = True
    assert raised

assert bytes.partition(source, b':') == (b'a', b':', b'b:c')
assert bytes.rpartition(source, b':') == (b'a:b', b':', b'c')
assert b'ababa'.partition(b'aba') == (b'', b'aba', b'ba')
assert b'ababa'.rpartition(b'aba') == (b'ab', b'aba', b'')
assert b'a\x00b'.partition(b'\x00') == (b'a', b'\x00', b'b')
for source in (B(b''), B(b'abc')):
    left = bytes.partition(source, b':')
    right = bytes.rpartition(source, b':')
    assert left[0] is source
    assert left[1] == b'' and left[2] == b''
    assert right[2] is source
    assert right[0] == b'' and right[1] == b''

# Partition keeps a mutable separator, while split outputs independent bytes.
sep = bytearray(b':')
partitioned = b'a:b'.partition(sep)
parts = b'a:b'.split(sep)
sep[0] = 33
assert partitioned[1] is sep
assert partitioned[1] == bytearray(b'!')
assert parts == [b'a', b'b']

# Direct methods validate their receiver; native binding rejects wrong arity.
for method in (bytes.split, bytes.rsplit, bytes.partition, bytes.rpartition):
    for receiver in ('abc', bytearray(b'abc'), 1, None):
        raised = False
        try:
            method(receiver, b':')
        except TypeError:
            raised = True
        assert raised
    raised = False
    try:
        method(b'a', b':', 1, 2)
    except TypeError:
        raised = True
    assert raised
for method in (bytes.partition, bytes.rpartition):
    raised = False
    try:
        method(b'a')
    except TypeError:
        raised = True
    assert raised

# Full-length exact bytes may be reused; subclasses must be normalized.
original = b'abc'
assert original.split(b':')[0] is original
assert original.rsplit(b':', 0)[0] is original
assert original.split()[0] is original
assert original.rsplit()[0] is original
assert b'  '.split(maxsplit=0) == []
assert b'  '.rsplit(maxsplit=0) == []
assert b'a:b:c'.split(maxsplit=1, sep=b':') == [b'a', b'b:c']
assert b'a:b:c'.rsplit(maxsplit=1, sep=b':') == [b'a:b', b'c']
for method in (bytes.split, bytes.rsplit):
    raised = False
    try:
        method(b'a', b':', sep=b':')
    except TypeError:
        raised = True
    assert raised
    raised = False
    try:
        method(b'a', unknown=1)
    except TypeError:
        raised = True
    assert raised

# Exhaust all byte values to keep ASCII whitespace separate from text rules.
for code in range(256):
    data = b'x' + bytes([code]) + b'x'
    expected = [b'x', b'x'] if code == 32 or 9 <= code <= 13 else [data]
    assert data.split() == expected
    assert data.rsplit() == expected

# Exercise cached method lookup with different receiver and separator shapes.
for source in (b'a:b:c', B(b'a:b:c'), b'a:b:c'):
    for sep in (b':', bytearray(b':'), B(b':')):
        assert source.rsplit(sep, 1) == [b'a:b', b'c']
        assert source.partition(sep)[1] is sep
