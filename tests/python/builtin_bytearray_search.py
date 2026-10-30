# Checked against CPython 3.14.5: Objects/bytearrayobject.c and bytes methods.
# Exercise direct calls and unbound native methods with explicit receivers.
methods = ('find', 'rfind', 'index', 'rindex', 'count', 'startswith', 'endswith')

# Stored payloads bypass subclass conversion, indexing and iteration hooks.
class Binary(bytes):
    def __index__(self):
        raise AssertionError
    def __bytes__(self):
        raise AssertionError
    def __len__(self):
        raise AssertionError
    def __getitem__(self, key):
        raise AssertionError
    def __iter__(self):
        raise AssertionError
class Integer(int):
    def __bytes__(self):
        raise AssertionError
    def __index__(self):
        raise AssertionError
    def __int__(self):
        raise AssertionError
class Candidates(tuple):
    def __len__(self):
        raise AssertionError
    def __getitem__(self, key):
        raise AssertionError
    def __iter__(self):
        raise AssertionError

def check_ranges():
    bounds = (None, -10**100, -6, -1, 0, 1, 3, 5, 6, 10**100)
    for data in (b'', b'a', b'ababa', b'\x00\xff\x00'):
        a = bytearray(data)
        for raw in (b'', b'a', b'aba', b'ababa', b'ababab', b'\x00', b'\xff'):
            for needle in (raw, bytearray(raw)):
                for start in bounds:
                    for end in bounds:
                        assert a.find(needle, start, end) == data.find(raw, start, end)
                        assert a.rfind(needle, start, end) == data.rfind(raw, start, end)
                        assert a.count(needle, start, end) == data.count(raw, start, end)
                        assert a.startswith(needle, start, end) == data.startswith(raw, start, end)
                        assert a.endswith(needle, start, end) == data.endswith(raw, start, end)
                        found = a.find(needle, start, end)
                        last = a.rfind(needle, start, end)
                        if found >= 0:
                            assert a.index(needle, start, end) == found
                        else:
                            try:
                                a.index(needle, start, end)
                            except ValueError as e:
                                assert str(e) == 'subsection not found'
                            else:
                                raise AssertionError
                        if last >= 0:
                            assert a.rindex(needle, start, end) == last
                        else:
                            try:
                                a.rindex(needle, start, end)
                            except ValueError as e:
                                assert str(e) == 'subsection not found'
                            else:
                                raise AssertionError
        for needle in (0, 1, 97, 255, False, True):
            for start in bounds:
                for end in bounds:
                    assert a.find(needle, start, end) == data.find(needle, start, end)
                    assert a.rfind(needle, start, end) == data.rfind(needle, start, end)
                    assert a.count(needle, start, end) == data.count(needle, start, end)
                    if a.find(needle, start, end) >= 0:
                        assert a.index(needle, start, end) == data.index(needle, start, end)
                        assert a.rindex(needle, start, end) == data.rindex(needle, start, end)
                    else:
                        for method in (bytearray.index, bytearray.rindex):
                            try:
                                method(a, needle, start, end)
                            except ValueError:
                                pass
                            else:
                                raise AssertionError


def check_tuples():
    # Tuple selection is lazy, not a validation pass over the whole tuple.
    a = bytearray(b'ababa')
    assert a.count(b'aba') == 1 and a.rfind(b'aba') == 2
    assert a.startswith((b'x', bytearray(b'ab'), None))
    assert a.endswith((b'x', bytearray(b'ba'), None))
    assert a.startswith((b'', None)) and a.endswith((b'', None))
    assert not a.startswith(()) and not a.endswith(())
    assert not a.startswith((b'ba', b'x')) and not a.endswith((b'ab', b'x'))
    for name in ('startswith', 'endswith'):
        method = getattr(bytearray, name)
        for candidate in ((None, b''), (b'x', None), ((b'',),), (b'', None)):
            for pair in ((6, None), (4, 2)):
                start = pair[0]
                end = pair[1]
                try:
                    method(a, candidate, start, end)
                except TypeError:
                    pass
                else:
                    raise AssertionError
        try:
            method(a, (None, b''))
        except TypeError:
            pass
        else:
            raise AssertionError


def check_subclasses():
    a = bytearray(b'ababa')
    assert a.find(Binary(b'aba')) == 0
    assert a.rindex(Binary(b'aba')) == 2
    assert a.count(Binary(b'aba')) == 1
    assert a.startswith(Candidates((b'x', Binary(b'ab'), None)))
    assert a.endswith(Candidates((b'x', Binary(b'ba'), None)))
    assert a.find(Integer(97), Integer(1), Integer(10**100)) == 2
    assert a.rindex(Integer(97), Integer(-10**100)) == 4
    assert a.count(Integer(97)) == 3
    assert a.find(b'', Integer(10**100)) == -1


def check_errors():
    a = bytearray(b'ababa')
    # Validation precedes range rejection, including huge integers and tuples.
    for name in methods:
        method = getattr(bytearray, name)
        for pair in (('bad', 10**100), (10**100, 'bad')):
            start = pair[0]
            end = pair[1]
            try:
                method(a, None, start, end)
            except TypeError as e:
                assert 'slice indices' in str(e)
            else:
                raise AssertionError
        for needle in (None, 'a', 1.0, [97]):
            try:
                method(a, needle, 6, 1)
            except TypeError:
                pass
            else:
                raise AssertionError
        for needle in (-1, 256, -10**100, 10**100, Integer(256)):
            try:
                method(a, needle, 6, 1)
            except ValueError:
                assert name in ('find', 'rfind', 'index', 'rindex', 'count')
            except TypeError:
                assert name in ('startswith', 'endswith')
            else:
                raise AssertionError
        # Arity, keywords, and receiver checks must not depend on contents.
        for args in ((), (b'a', 0, 3, 4)):
            try:
                method(a, *args)
            except TypeError:
                pass
            else:
                raise AssertionError
        try:
            method(a, b'a', start=0)
        except TypeError:
            pass
        else:
            raise AssertionError
        try:
            getattr(bytearray, name)(b'abc', None, 'bad')
        except TypeError as e:
            assert 'slice indices' not in str(e)
        else:
            raise AssertionError


def check_mutation():
    # No views or cached search facts survive between calls or across mutations.
    for i in range(4):
        a = bytearray(b'ababa')
        needle = bytearray(b'aba')
        assert a.find(a) == 0 and a.rfind(a) == 0 and a.count(a) == 1
        assert a.startswith((a,)) and a.endswith((a,))
        assert a.find(needle) == 0 and a.rfind(needle) == 2 and a.count(needle) == 1
        needle.clear()
        assert a.find(needle) == 0 and a.rfind(needle) == 5 and a.count(needle) == 6
        needle.extend(b'ba')
        a.clear()
        for repeat in range(128):
            a.extend(b'ba')
        assert a.find(needle) == 0 and a.rfind(needle) == 254 and a.count(needle) == 128
        assert a.startswith((needle,)) and a.endswith((needle,))
        a.reverse()
        assert a.find(needle) == 1 and a.rfind(needle) == 253 and a.count(needle) == 127
        assert not a.startswith(needle) and not a.endswith(needle)
        a.clear()
        assert a.find(a) == 0 and a.rfind(a) == 0 and a.count(a) == 1
        assert a.startswith(a) and a.endswith(a)


def check_alphabet():
    # Signed-char and zero-byte handling across the entire byte alphabet.
    a = bytearray(range(256))
    for byte in range(256):
        needle = bytearray([byte])
        assert a.find(byte) == byte and a.rfind(needle) == byte
        assert a.index(needle) == byte and a.rindex(byte) == byte
        assert a.count(needle) == 1
        assert a.startswith((needle,), byte, byte + 1)
        assert a.endswith(needle, byte, byte + 1)

    a = bytearray(b'\x00\x01')
    assert a.find(True, False, True) == -1
    assert a.index(True, True, None) == 1
    assert a.count(False, False, True) == 1
    assert a.startswith(b'\x00', False, True)
    assert a.endswith(b'\x01', True, None)


check_ranges()
check_tuples()
check_subclasses()
check_errors()
check_mutation()
check_alphabet()
