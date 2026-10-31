# Intentional stored-payload and existing-codec boundaries, not CPython parity.
from urllib import parse

class Text(str):
    def encode(self, *args):
        raise AssertionError('encode hook')
    def __len__(self):
        raise AssertionError('len hook')
    def __getitem__(self, key):
        raise AssertionError('item hook')
    def __contains__(self, key):
        raise AssertionError('contains hook')
    def __iter__(self):
        raise AssertionError('iter hook')
class Binary(bytes):
    def __len__(self):
        raise AssertionError('len hook')
    def __getitem__(self, key):
        raise AssertionError('item hook')
    def __contains__(self, key):
        raise AssertionError('contains hook')
    def __iter__(self):
        raise AssertionError('iter hook')
    def rstrip(self, *args):
        raise AssertionError('rstrip hook')
    def decode(self, *args):
        raise AssertionError('decode hook')
    def split(self, *args):
        raise AssertionError('split hook')
assert parse.quote(Text('a b'), Text(' ')) == 'a b'
assert parse.quote_plus(Text('a b')) == 'a+b'
empty = Text('')
assert parse.quote(empty) is empty
assert parse.quote_from_bytes(Binary(b'a b')) == 'a%20b'
assert parse.quote_plus(Binary(b'a b')) == 'a+b'
assert parse.unquote_to_bytes(Binary(b'a%20b')) == b'a b'
assert parse.unquote_to_bytes(Text('a%20b')) == b'a b'
for f in (parse.quote, parse.quote_plus):
    try:
        f('a', errors='replace')
        assert False
    except NotImplementedError:
        pass
    try:
        f('a', encoding='ascii')
        assert False
    except LookupError:
        pass
    assert f('', encoding='ascii', errors='replace') == ''

# Safe byte iterables deliberately retain normal iterator behavior.
class SafeBytes(bytes):
    def __iter__(self):
        return iter([32])
assert parse.quote_from_bytes(b'a b', SafeBytes(b'')) == 'a b'

# The module's payload/character operations survive public builtin rebinding.
import builtins
saved = (builtins.bytes, builtins.bytearray, builtins.str, builtins.isinstance,
         builtins.len, builtins.ord, builtins.range, builtins.slice, builtins.chr)
try:
    builtins.bytes = None
    builtins.bytearray = None
    builtins.str = None
    builtins.isinstance = None
    builtins.len = None
    builtins.ord = None
    builtins.range = None
    builtins.slice = None
    builtins.chr = None
    assert parse.quote('a b') == 'a%20b'
    assert parse.quote_plus(b'a b') == 'a+b'
    assert parse.unquote_to_bytes('a%20b') == b'a b'
finally:
    builtins.bytes = saved[0]
    builtins.bytearray = saved[1]
    builtins.str = saved[2]
    builtins.isinstance = saved[3]
    builtins.len = saved[4]
    builtins.ord = saved[5]
    builtins.range = saved[6]
    builtins.slice = saved[7]
    builtins.chr = saved[8]
