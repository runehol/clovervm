# Portions adapted from tests/python/bytes_split.py and its CPython
# Lib/test/string_tests.py v3.14.5 cases.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Behavioral reference: CPython 3.14.10 bytearray methods and stringlib splitting.

def check_split_arguments():
    for method in (bytearray.split, bytearray.rsplit):
        assert method(bytearray()) == []
        assert method(bytearray(), b':') == [bytearray()]
        assert method(bytearray(b' \t\n\r\v\f ')) == []
        assert method(bytearray(b'a:b::c:'), b':') == [b'a', b'b', b'', b'c', b'']
        assert method(bytearray(b'a//b//c'), b'//') == [b'a', b'b', b'c']
        assert method(bytearray(b'a\x00b'), b'\x00') == [b'a', b'b']
        assert method(bytearray(b'abc'), b'longer') == [b'abc']
        assert method(bytearray(b'a:b'), sep=b':', maxsplit=1) == [b'a', b'b']
        assert method(bytearray(b'a:b'), maxsplit=1, sep=bytearray(b':')) == [b'a', b'b']
        assert method(bytearray(b'a:b'), b':', False) == [b'a:b']
        for limit in (-9223372036854775808, -2, -1, 288230376151711744,
                      9223372036854775807):
            assert method(bytearray(b'a:b:c'), b':', limit) == [b'a', b'b', b'c']
        for sep in (b'', bytearray()):
            for limit in (0, -1, 1):
                raised = False
                try:
                    method(bytearray(), sep, limit)
                except ValueError:
                    raised = True
                assert raised
        for limit in (9223372036854775808, -9223372036854775809, 10**100, -10**100):
            for sep in (None, b'', [], bytearray()):
                raised = False
                try:
                    method(bytearray(), sep, limit)
                except OverflowError:
                    raised = True
                assert raised
        for limit in (None, '1', 1.0, []):
            for sep in (b'', []):
                raised = False
                try:
                    method(bytearray(), sep, limit)
                except TypeError:
                    raised = True
                assert raised
        for sep in (1, True, 'a', [], (b'a',)):
            raised = False
            try:
                method(bytearray(b'a'), sep)
            except TypeError:
                raised = True
            assert raised


def check_split_values():
    assert bytearray(b'  a b  ').split(None, 0) == [b'a b  ']
    assert bytearray(b'  a b  ').rsplit(None, 0) == [b'  a b']
    assert bytearray(b'  a b  ').split(None, 1) == [b'a', b'b  ']
    assert bytearray(b'  a b  ').rsplit(None, 1) == [b'  a', b'b']
    assert bytearray(b'ababa').split(b'aba') == [b'', b'ba']
    assert bytearray(b'ababa').rsplit(b'aba') == [b'ab', b'']
    assert bytearray(b'a:b:c').split(b':', True) == [b'a', b'b:c']
    assert bytearray(b'a:b:c').rsplit(b':', True) == [b'a:b', b'c']
    assert bytearray(b'a:b:c').partition(b':') == (b'a', b':', b'b:c')
    assert bytearray(b'a:b:c').rpartition(b':') == (b'a:b', b':', b'c')
    assert bytearray(b'ababa').partition(b'aba') == (b'', b'aba', b'ba')
    assert bytearray(b'ababa').rpartition(b'aba') == (b'ab', b'aba', b'')
    assert bytearray(b'a\x00b').partition(b'\x00') == (b'a', b'\x00', b'b')
    assert bytearray(b'abc').partition(b':') == (b'abc', b'', b'')
    assert bytearray(b'abc').rpartition(b':') == (b'', b'', b'abc')


class B(bytes):
    def __bytes__(self):
        raise AssertionError
    def __len__(self):
        raise AssertionError
    def __getitem__(self, key):
        raise AssertionError
    def __iter__(self):
        raise AssertionError

class I(int):
    def __index__(self):
        raise AssertionError
    def __int__(self):
        raise AssertionError

for method in (bytearray.split, bytearray.rsplit):
    assert method(bytearray(b'a:b'), B(b':'), I(1)) == [b'a', b'b']

def check_independence():
    # Every result is independent of source, separator, and all other pieces.
    # Includes empty, no-match, full-match, partial-match, and self-alias cases.
    for method in (bytearray.split, bytearray.rsplit,
                   bytearray.partition, bytearray.rpartition):
        for data in (b'', b'abc', b':', b'a:b:c', b'::', b'ababa'):
            for separator in (b':', b'longer', B(b'aba'), bytearray(b':')):
                source = bytearray(data)
                parts = method(source, separator)
                if method is bytearray.split or method is bytearray.rsplit:
                    assert parts.__class__ is list
                else:
                    assert parts.__class__ is tuple
                original = []
                for part in parts:
                    original.append(bytes(part))
                sep_copy = bytes(bytearray(separator))
                for index in range(len(parts)):
                    part = parts[index]
                    assert part.__class__ is bytearray
                    assert part is not source and part is not separator
                    for other in range(len(parts)):
                        if other != index:
                            assert part is not parts[other]
                    part.append(33)
                    assert source == data and separator == sep_copy
                    for other in range(len(parts)):
                        if other != index:
                            assert parts[other] == original[other]
                    part.pop()
                source.append(42)
                if separator.__class__ is bytearray:
                    separator.append(43)
                for index in range(len(parts)):
                    assert parts[index] == original[index]
        source = bytearray(b'ababa')
        parts = method(source, source)
        source.clear()
        for part in parts:
            assert part.__class__ is bytearray and part is not source
        if method is bytearray.partition or method is bytearray.rpartition:
            assert parts == (b'', b'ababa', b'')
        else:
            assert parts == [b'', b'']
        parts[0].append(44)
        assert parts[-1] == b''
        for invalid_receiver in (b'a', 'a', 1, None):
            raised = False
            try:
                method(invalid_receiver, b':')
            except TypeError:
                raised = True
            assert raised
        raised = False
        try:
            method(bytearray(), b':', 1, 2)
        except TypeError:
            raised = True
        assert raised


def check_zero_limits():
    # Whitespace and maxsplit=0 copies never share the original storage either.
    for method in (bytearray.split, bytearray.rsplit):
        for separator in (None, b':', bytearray(b':')):
            for data in (b'', b'abc', b'  a b  ', b'   '):
                source = bytearray(data)
                parts = method(source, separator, 0)
                for part in parts:
                    assert part.__class__ is bytearray and part is not source
                    part.append(33)
                    assert source == data
        raised = False
        try:
            method(bytearray(), b':', sep=b':')
        except TypeError:
            raised = True
        assert raised
        raised = False
        try:
            method(bytearray(), unknown=1)
        except TypeError:
            raised = True
        assert raised


def check_partition_errors():
    for method in (bytearray.partition, bytearray.rpartition):
        for sep in (b'', bytearray()):
            raised = False
            try:
                method(bytearray(), sep)
            except ValueError:
                raised = True
            assert raised
        for sep in (None, 1, True, ':', [], (b':',)):
            raised = False
            try:
                method(bytearray(b'a'), sep)
            except TypeError:
                raised = True
            assert raised
        raised = False
        try:
            method(bytearray())
        except TypeError:
            raised = True
        assert raised
        raised = False
        try:
            method(bytearray(), sep=b':')
        except TypeError:
            raised = True
        assert raised


def check_alphabet():
    for code in range(256):
        data = b'x' + bytes([code]) + b'x'
        expected = [b'x', b'x'] if code == 32 or 9 <= code <= 13 else [data]
        assert bytearray(data).split() == expected
        assert bytearray(data).rsplit() == expected


# Repeated lookup/calls across supported shapes and source mutations.
def split_again(source, sep):
    return source.rsplit(sep, 1)

def partition_again(source, sep):
    return source.partition(sep)

def check_repeated_calls():
    for repetition in range(110):
        for separator in (b':', B(b':'), bytearray(b':')):
            source = bytearray(b'a:b:c')
            assert split_again(source, separator) == [b'a:b', b'c']
            assert partition_again(source, separator) == (b'a', b':', b'b:c')
            source[1] = 33
            assert partition_again(source, separator) == (b'a!b', b':', b'c')
            source.clear()
            assert split_again(source, separator) == [b'']
            assert partition_again(source, separator) == (b'', b'', b'')


# Enter splitting from a managed iterator callback used by construction/extend.
# The C++ harness runs this file with reclamation requested at every safepoint.
class SplitSource:
    def __init__(self):
        self.index = 0
    def __iter__(self):
        return self
    def __next__(self):
        if self.index == 2:
            raise StopIteration
        source = bytearray(b'a:b')
        separator = bytearray(b':')
        parts = source.split(separator)
        partitioned = source.rpartition(separator)
        source.clear()
        separator.clear()
        assert parts == [b'a', b'b']
        assert partitioned == (b'a', b':', b'b')
        value = parts[self.index][0]
        self.index += 1
        return value

def check_reentry():
    assert bytearray(SplitSource()) == b'ab'
    reentered = bytearray(b'x')
    reentered.extend(SplitSource())
    assert reentered == b'xab'


def check_mixed_receivers():
    for source in (b'a:b:c', bytearray(b'a:b:c'), b'a:b:c', bytearray(b'a:b:c')):
        assert split_again(source, bytearray(b':')) == [b'a:b', b'c']
        result = partition_again(source, bytearray(b':'))
        assert result == (b'a', b':', b'b:c')
        if source.__class__ is bytearray:
            for part in result:
                assert part.__class__ is bytearray



# Keep each code object below the existing 256 global-read-cache-site limit.
check_split_arguments()
check_split_values()
check_independence()
check_zero_limits()
check_partition_errors()
check_alphabet()
check_repeated_calls()
check_reentry()
check_mixed_receivers()
