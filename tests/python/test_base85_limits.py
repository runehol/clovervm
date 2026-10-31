# CloverVM's agreed stored-payload boundary, intentionally beyond CPython hooks.
import base64


class StoredBytes(bytes):
    def __len__(self):
        raise AssertionError('length hook')
    def __getitem__(self, key):
        raise AssertionError('index hook')
    def __add__(self, other):
        raise AssertionError('addition hook')
    def __bytes__(self):
        raise AssertionError('conversion hook')


class StoredString(str):
    def __len__(self):
        raise AssertionError('length hook')
    def __getitem__(self, key):
        raise AssertionError('index hook')
    def encode(self, *args):
        raise AssertionError('encoding hook')


for case in ((b'f', b'W&'), (b'foob', b'W^Zp|')):
    raw = case[0]
    encoded = case[1]
    assert base64.b85encode(StoredBytes(raw)) == encoded
    assert base64.b85decode(StoredBytes(encoded)) == raw
assert base64.b85decode(StoredString('W&')) == b'f'


class Conversion:
    def __bytes__(self):
        raise AssertionError('unsupported conversion must not run')
    def __iter__(self):
        raise AssertionError('unsupported iteration must not run')


for call in (base64.b85encode, base64.b85decode):
    try:
        call(Conversion())
    except TypeError:
        pass
    else:
        raise AssertionError('unsupported custom conversion')
