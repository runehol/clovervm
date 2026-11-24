# CloverVM-specific limits and callback behavior.
class Index:
    def __index__(self):
        raise AssertionError('custom index protocol is excluded')


for data in (b'abc', bytearray(b'abc')):
    try:
        data.hex(':', Index())
        assert False, 'expected TypeError'
    except TypeError:
        pass


data = bytearray(b'abc')


class ResizingSeparator(bytes):
    def __len__(self):
        data[0] = 255
        data.append(0)
        assert data.hex() == 'ff626300'
        return 1


assert data.hex(ResizingSeparator(b':'), 3) == 'ff:626300'
assert data == b'\xffbc\x00'


class ClearingSeparator(str):
    def __len__(self):
        data.clear()
        return 1


assert data.hex(ClearingSeparator(':')) == ''
assert data == b''
