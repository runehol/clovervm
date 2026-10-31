# Agreed stored-payload limits, intentionally bypassing CPython subclass hooks.
import base64

class StoredBytes(bytes):
    def __len__(self):
        raise AssertionError('length hook')
    def __getitem__(self, key):
        raise AssertionError('index hook')
    def __add__(self, other):
        raise AssertionError('addition hook')
    def startswith(self, prefix):
        raise AssertionError('framing hook')
    def endswith(self, suffix):
        raise AssertionError('framing hook')
class StoredString(str):
    def __len__(self):
        raise AssertionError('length hook')
    def __getitem__(self, key):
        raise AssertionError('index hook')
    def encode(self, *args):
        raise AssertionError('encoding hook')
assert base64.a85encode(StoredBytes(b'foo')) == b'AoDS'
assert base64.a85decode(StoredBytes(b'<~AoDS~>'), adobe=True) == b'foo'
assert base64.a85decode(StoredString('AoDS')) == b'foo'
class Conversion:
    def __bytes__(self):
        raise AssertionError('conversion hook')
    def __iter__(self):
        raise AssertionError('iteration hook')
for call in (base64.a85encode, base64.a85decode):
    try:
        call(Conversion())
    except TypeError:
        pass
    else:
        raise AssertionError('unsupported conversion')
