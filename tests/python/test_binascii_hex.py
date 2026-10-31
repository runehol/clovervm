# Independent regression cases checked against CPython 3.14.10.
import binascii


def fails(error_type, message, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
        assert False, 'expected exception'
    except error_type as error:
        if message is not None:
            assert error.args == (message,)


assert binascii.hexlify is not binascii.b2a_hex
assert binascii.unhexlify is not binascii.a2b_hex
for encode in (binascii.hexlify, binascii.b2a_hex):
    assert encode(data=b'abc') == b'616263'
    assert encode(b'abc', bytes_per_sep=2) == b'616263'
    assert encode(b'abcde', b':', 2) == b'61:6263:6465'
    assert encode(b'abcde', ':', -2) == b'6162:6364:65'
    assert encode(b'abc', b'\xff') == b'61\xff62\xff63'
    assert encode(b'abc', '\xff') == b'61\xff62\xff63'
    assert encode(b'abc', b'\0') == b'61\x0062\x0063'
    for group in (0, 2147483647, -2147483648):
        assert encode(b'abc', b':', group) == b'616263'
    assert encode(b'ab', b':', True) == b'61:62'
    for group in (-2147483649, 2147483648, 10 ** 100):
        fails(OverflowError, 'Python int too large to convert to C int',
              encode, b'', None, group)
    fails(TypeError, None, encode, None, None, 1.0)
    fails(TypeError, None, encode, b'', None, 1.0)
    fails(TypeError, None, encode, b'', None)
    fails(ValueError, 'sep must be length 1.', encode, b'', b'', 0)
    fails(ValueError, 'sep must be length 1.', encode, b'', [], 0)
    fails(TypeError, 'sep must be str or bytes.', encode, b'', [1], 0)
    fails(TypeError, 'sep must be str or bytes.', encode, b'', bytearray(b':'))
    fails(ValueError, 'sep must be ASCII.', encode, b'', '\u0100')
    fails(TypeError, None, encode)
    fails(TypeError, None, encode, b'', b':', 1, 1)
    fails(TypeError, None, encode, b'', data=b'')
    fails(TypeError, None, encode, b'', unknown=1)

for decode in (binascii.unhexlify, binascii.a2b_hex):
    for data in (b'', '', bytearray()):
        assert decode(data) == b''
    for data in (b'aB01fF', 'aB01fF', bytearray(b'aB01fF')):
        assert decode(data) == b'\xab\x01\xff'
    for data in (b'x', b'\xff', 'x'):
        fails(binascii.Error, 'Odd-length string', decode, data)
    for data in (b'x0', b'\xff0', b' 0', b'0\n', b'0\0', '0:'):
        fails(binascii.Error, 'Non-hexadecimal digit found', decode, data)
    for data in ('\xe9', '\xe90', '\ud800'):
        fails(ValueError, 'string argument should contain only ASCII characters', decode, data)
    fails(TypeError, None, decode, None)
    fails(TypeError, None, decode, hexstr=b'00')
    fails(TypeError, None, decode, b'', b'')

# Full byte range, grouping directions, lengths and mutable inputs.
raw = bytes(list(range(256)))
for size in (0, 1, 2, 3, 7, 16, 255, 256):
    data = raw[:size]
    assert binascii.unhexlify(binascii.hexlify(data)) == data
    assert binascii.hexlify(bytearray(data)) == binascii.hexlify(data)
    for group in (-17, -3, -1, 0, 1, 2, 17):
        result = binascii.hexlify(data, b':', group)
        plain = []
        for i in range(len(result)):
            if result[i] != 58:
                plain.append(result[i])
        assert binascii.unhexlify(bytes(plain)) == data
        expected_separators = (size - 1) // abs(group) if group and size else 0
        assert len(result) == 2 * size + expected_separators

class Payload(bytes):
    def __len__(self):
        raise AssertionError('payload len override')
    def __getitem__(self, key):
        raise AssertionError('payload item override')
    def __bytes__(self):
        raise AssertionError('payload bytes override')

class Text(str):
    def __len__(self):
        raise AssertionError('text len override')
    def __getitem__(self, key):
        raise AssertionError('text item override')

class Group(int):
    def __index__(self):
        raise AssertionError('group index override')
    def __lt__(self, other):
        raise AssertionError('group comparison override')

assert binascii.hexlify(Payload(b'ab'), b':', Group(1)) == b'61:62'
assert binascii.unhexlify(Payload(b'aB')) == b'\xab'
assert binascii.unhexlify(Text('aB')) == b'\xab'

payload = bytearray(b'abc')
calls = []
class Separator(bytes):
    def __len__(self):
        calls.append('len')
        payload[0] = 120
        return 1
    def __getitem__(self, key):
        raise AssertionError('separator item override')

assert binascii.hexlify(payload, Separator(b':')) == b'78:62:63'
assert calls == ['len']
class RaisingSeparator(str):
    def __len__(self):
        raise LookupError('separator')
fails(LookupError, 'separator', binascii.hexlify, b'', RaisingSeparator(':'))
# Input validation precedes grouping conversion; grouping precedes callbacks.
fails(TypeError, None, binascii.hexlify, None, b':', 10 ** 100)
fails(TypeError, None, binascii.hexlify, b'ab', RaisingSeparator(':'), 1.0)

# Shared error identity, including existing Base16 users.
import base64
assert base64.binascii.Error is binascii.Error
fails(binascii.Error, None, base64.b16decode, b'zz')

# Separator subclasses can report a length different from their stored payload.
class ReportedText(str):
    def __len__(self):
        return 1
class ReportedBytes(bytes):
    def __len__(self):
        return 1
for sep in (ReportedText(''), ReportedBytes(b'')):
    assert binascii.hexlify(b'ab', sep) == b'61\x0062'
assert binascii.hexlify(b'ab', ReportedText('ab')) == b'61a62'
assert binascii.hexlify(b'ab', ReportedBytes(b'ab')) == b'61a62'
fails(ValueError, 'sep must be ASCII.', binascii.hexlify,
      b'ab', ReportedText('a\u0100'))

# Warm callback lookup, then change the separator length implementation.
class DynamicSeparator(bytes):
    def __len__(self):
        return 1
separator = DynamicSeparator(b':')
for i in range(8):
    assert binascii.hexlify(b'ab', separator) == b'61:62'
def empty_length(self):
    return 0
DynamicSeparator.__len__ = empty_length
fails(ValueError, 'sep must be length 1.', binascii.hexlify, b'ab', separator)
