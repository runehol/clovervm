# Behavioral reference: CPython 3.14.10 bytes/bytearray stripping.
# Original tests; no CPython test content copied.

class StripBytes(bytes):
    def __bytes__(self):
        raise AssertionError('conversion hook')
    def __len__(self):
        raise AssertionError('length hook')
    def __getitem__(self, key):
        raise AssertionError('index hook')
    def __iter__(self):
        raise AssertionError('iteration hook')


class PretendBinary:
    def __bytes__(self):
        raise AssertionError('conversion hook')
    def __len__(self):
        raise AssertionError('length hook')
    def __iter__(self):
        raise AssertionError('iteration hook')


def check_values(cls):
    source = cls(b' \t\n\r\v\fabc \t\n\r\v\f')
    assert source.strip() == b'abc'
    assert source.strip(None) == b'abc'
    assert source.lstrip() == b'abc \t\n\r\v\f'
    assert source.rstrip() == b' \t\n\r\v\fabc'
    assert cls(b'\x1c\x85\xa0').strip() == b'\x1c\x85\xa0'
    assert cls(b'abbaZbaab').strip(b'ab') == b'Z'
    assert cls(b'abbaZbaab').lstrip(bytearray(b'ba')) == b'Zbaab'
    assert cls(b'abbaZbaab').rstrip(StripBytes(b'ab')) == b'abbaZ'
    assert cls(b'\x00\xffZ\xff\x00').strip(b'\xff\x00') == b'Z'
    assert cls(b'abc').strip(b'') == b'abc'
    assert cls(b'abc').strip(bytearray()) == b'abc'
    for name in ('strip', 'lstrip', 'rstrip'):
        method = getattr(cls, name)
        for data in (b'', b'abc', b'abba', b' \t\n\r\v\f'):
            source = cls(data)
            assert method(source, source) == b''
            assert method(source, source).__class__ is cls
            for chars in (None, b'', bytearray(), b'ab', StripBytes(b'ab')):
                result = method(source, chars)
                assert result.__class__ is cls
                if cls is bytes:
                    assert (result is source) == (result == source)
                else:
                    assert result is not source
                    original = bytes(source)
                    result.append(255)
                    assert bytes(source) == original
                    source.append(0)
                    assert result[-1] == 255
        for invalid in (0, True, 'a', [], (b'a',), 1.0, PretendBinary()):
            # Invalid chars must be rejected even with an empty receiver.
            for data in (b'', b'abc'):
                raised = False
                try:
                    method(cls(data), invalid)
                except TypeError:
                    raised = True
                assert raised
        raised = False
        try:
            method(cls(), b'', b'')
        except TypeError:
            raised = True
        assert raised
        raised = False
        try:
            method(cls(), bytes=b'')
        except TypeError:
            raised = True
        assert raised
        raised = False
        try:
            method(cls(), chars=b'')
        except TypeError:
            raised = True
        assert raised
        raised = False
        try:
            method('bad receiver', b'')
        except TypeError:
            raised = True
        assert raised
        raised = False
        try:
            method()
        except TypeError:
            raised = True
        assert raised


def check_alphabet(cls):
    alphabet = bytearray()
    for value in range(256):
        alphabet.append(value)
        one = bytearray()
        one.append(value)
        byte = bytes(one)
        source = cls(byte + b'Z' + byte)
        for chars in (byte, one):
            if value == 90:
                assert source.strip(chars) == b''
                assert source.lstrip(chars) == b''
                assert source.rstrip(chars) == b''
            else:
                assert source.strip(chars) == b'Z'
                assert source.lstrip(chars) == b'Z' + byte
                assert source.rstrip(chars) == byte + b'Z'
        if value in (9, 10, 11, 12, 13, 32):
            assert cls(byte).strip() == b''
        else:
            assert cls(byte).strip() == byte
    for name in ('strip', 'lstrip', 'rstrip'):
        assert getattr(cls, name)(cls(alphabet), alphabet) == b''
        assert getattr(cls, name)(cls(alphabet), bytes(alphabet)) == b''


check_values(bytes)
check_values(bytearray)
check_alphabet(bytes)
check_alphabet(bytearray)

for name in ('strip', 'lstrip', 'rstrip'):
    method = getattr(bytes, name)
    source = StripBytes(b'abc')
    assert method(source, b'').__class__ is bytes
    assert method(source, b'') == b'abc'
    assert method(source, b'') is not source
    assert method(StripBytes(b'abZba'), StripBytes(b'ab')) == getattr(bytes, name)(b'abZba', b'ab')
    assert method(StripBytes(b''), None).__class__ is bytes

# A mutable charset and source remain unchanged and independent of the result.
for name in ('strip', 'lstrip', 'rstrip'):
    source = bytearray(b'abZba')
    chars = bytearray(b'ab')
    result = getattr(bytearray, name)(source, chars)
    snapshot = bytes(result)
    chars[0] = 90
    source[0] = 90
    assert result == snapshot

# Shared scanner extraction must preserve the existing string behavior.
class StripString(str):
    def __len__(self):
        raise AssertionError('length hook')
    def __getitem__(self, key):
        raise AssertionError('index hook')

for cls in (str, StripString):
    assert cls(' \t\n\r\v\fhello \t\n\r\v\f').strip() == 'hello'
    assert cls('abbaZbaab').strip('ab') == 'Z'
    assert cls('abbaZbaab').lstrip('ab') == 'Zbaab'
    assert cls('abbaZbaab').rstrip('ab') == 'abbaZ'
    assert cls('\x00\xff\u2028Z\u2028\xff\x00').strip('\x00\xff\u2028') == 'Z'
    assert cls('abc').strip('') == 'abc'
    assert cls('').strip('') == ''
    assert cls('abc').strip().__class__ is str
    assert cls('abc').strip(StripString('ac')) == 'b'
