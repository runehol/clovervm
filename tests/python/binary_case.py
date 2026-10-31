# Original tests, behavioral reference: CPython 3.14.10.

class HostileBytes(bytes):
    def __bytes__(self):
        raise AssertionError('bytes hook')
    def __len__(self):
        raise AssertionError('len hook')
    def __getitem__(self, key):
        raise AssertionError('getitem hook')
    def __iter__(self):
        raise AssertionError('iter hook')


def check_case():
    names = ('lower', 'upper', 'swapcase', 'capitalize', 'title')
    alphabet = bytearray()
    for value in range(256):
        alphabet.append(value)
        one = bytearray()
        one.append(value)
        byte = bytes(one)
        low = value + 32 if 65 <= value <= 90 else value
        high = value - 32 if 97 <= value <= 122 else value
        lo = bytearray()
        lo.append(low)
        up = bytearray()
        up.append(high)
        swapped = lo if 65 <= value <= 90 else up
        cased = 65 <= value <= 90 or 97 <= value <= 122
        for cls in (bytes, bytearray):
            source = cls(byte)
            assert source.lower() == lo
            assert source.upper() == up
            assert source.swapcase() == swapped
            assert source.capitalize() == up
            assert source.title() == up
            assert cls(b'a' + byte + b'B').title() == b'A' + bytes(lo) + (b'b' if cased else b'B')
            assert cls(byte + b'AB').capitalize() == bytes(up) + b'ab'
    for cls in (bytes, bytearray):
        for data in (b'', b'a', b'A', b'123!', b'abc', bytes(alphabet),
                     b"heLLo 12wORLD-isN'T\x00a\xffBC\x80dE"):
            for name in names:
                method = getattr(cls, name)
                source = cls(data)
                alias = source
                result = method(source)
                again = method(source)
                assert result.__class__ is cls
                assert source == data and alias == data
                assert result == getattr(bytes, name)(HostileBytes(data))
                if cls is bytes:
                    assert (result is source) == (len(data) == 0)
                    if len(data):
                        assert result is not again
                else:
                    assert result is not source and result is not again
                    saved = bytes(result)
                    source.append(255)
                    assert result == saved
                    result.append(0)
                    assert source == data + b'\xff'
        for name in names:
            method = getattr(cls, name)
            raised = False
            try:
                method()
            except TypeError:
                raised = True
            assert raised
            other = bytearray() if cls is bytes else b''
            for invalid in (None, 1, True, 'text', [], object(), other):
                raised = False
                try:
                    method(invalid)
                except TypeError:
                    raised = True
                assert raised
            raised = False
            try:
                method(cls(), 1)
            except TypeError:
                raised = True
            assert raised
            raised = False
            try:
                method(cls(), extra=1)
            except TypeError:
                raised = True
            assert raised
    for name in names:
        for data in (b'', b'abC', b'\xffABC'):
            source = HostileBytes(data)
            result = getattr(bytes, name)(source)
            assert result is not source
            assert result.__class__ is bytes
            assert result == getattr(bytes, name)(bytes(data))

# Exercise normal lookup repeatedly before replacing an inherited method.
class ReplaceCase(bytes):
    pass


def call_lower(value):
    return value.lower()


for iteration in range(100):
    assert call_lower(ReplaceCase(b'ABC')) == b'abc'


def replacement(self):
    return b'overridden'


ReplaceCase.lower = replacement
assert call_lower(ReplaceCase(b'ABC')) == b'overridden'
assert bytes.lower(ReplaceCase(b'ABC')) == b'abc'
del ReplaceCase.lower
assert call_lower(ReplaceCase(b'ABC')) == b'abc'

check_case()
