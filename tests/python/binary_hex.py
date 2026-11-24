# Independent regression cases checked against CPython 3.14.10.
import binascii


def fails(error_type, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
        assert False, 'expected exception'
    except error_type:
        pass


class RaisingSeparator(bytes):
    def __len__(self):
        raise LookupError('separator length')


class ReportedBytes(bytes):
    def __len__(self):
        return 1

    def __getitem__(self, index):
        raise AssertionError('separator must use stored bytes')


class ReportedText(str):
    def __len__(self):
        return 1

    def __getitem__(self, index):
        raise AssertionError('separator must use stored code points')


class Group(int):
    def __index__(self):
        raise AssertionError('group must use stored integer')


for cls in (bytes, bytearray):
    data = cls(b'abc')
    result = data.hex()
    assert result == '616263' and result.__class__ is str
    assert cls.hex(data, bytes_per_sep=2) == result
    saved = data.hex
    assert saved(bytes_per_sep=-2) == result
    assert saved(sep=':') == '61:62:63'
    assert saved(sep=b':', bytes_per_sep=2) == '61:6263'
    assert saved(':', bytes_per_sep=-2) == '6162:63'
    assert saved(':', Group(-2)) == '6162:63'
    assert saved(':', True) == '61:62:63'
    assert saved(b'\x00') == '61\x0062\x0063'
    assert saved(ReportedBytes(b':!')) == '61:62:63'
    assert saved(ReportedText(':\xe9')) == '61:62:63'
    assert saved(ReportedBytes(b'')) == '61\x0062\x0063'
    assert saved(ReportedText('')) == '61\x0062\x0063'
    for group in (0, 2147483647, -2147483648):
        assert saved(':', group) == result
    assert cls(b'').hex(':', -2) == ''

    for value in (data, cls(b'')):
        for separator in ('\xe9', '\u0100', b'\xff', ReportedText(':\u0100')):
            fails(ValueError, value.hex, separator, 0)
        fails(TypeError, value.hex, sep=None)
        fails(TypeError, value.hex, bytearray(b':'))
        fails(TypeError, value.hex, [1])
        fails(ValueError, value.hex, [])
        fails(ValueError, value.hex, '')
        fails(ValueError, value.hex, b'::')
        fails(LookupError, value.hex, RaisingSeparator(b':'), 0)
        fails(TypeError, value.hex, RaisingSeparator(b':'), 1.0)
        for group in (-2147483649, 2147483648, 10 ** 100):
            fails(OverflowError, value.hex, bytes_per_sep=group)
            fails(OverflowError, value.hex, RaisingSeparator(b':'), group)

    fails(TypeError, cls.hex, self=data)
    fails(TypeError, cls.hex, None, RaisingSeparator(b':'), 10 ** 100)
    fails(TypeError, saved, ':', 1, 2)
    fails(TypeError, saved, ':', sep=':')
    fails(TypeError, saved, unknown=1)
    assert data == b'abc'


class Payload(bytes):
    def __len__(self):
        raise AssertionError('receiver must use stored length')

    def __getitem__(self, index):
        raise AssertionError('receiver must use stored bytes')


assert Payload(b'abc').hex(':') == '61:62:63'

raw = bytes(list(range(256)))
for size in (0, 1, 2, 7, 16, 255, 256):
    data = raw[:size]
    mutable = bytearray(data)
    for group in (-17, -3, -1, 0, 1, 2, 17):
        expected = binascii.hexlify(data, ':', group).decode()
        assert data.hex(':', group) == expected
        assert mutable.hex(':', group) == expected
