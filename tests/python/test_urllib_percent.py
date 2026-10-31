# Percent-conversion cases, including fixtures adapted from CPython 3.14.10
# Lib/test/test_urllib.py. Copyright (c) Python Software Foundation.
# Derived portions are licensed under the PSF License Agreement.
from urllib import parse

assert parse.quote_from_bytes(b'abc def?') == 'abc%20def%3F'
assert parse.quote('alpha+beta gamma') == 'alpha%2Bbeta%20gamma'
assert parse.quote_plus('alpha+beta gamma') == 'alpha%2Bbeta+gamma'
assert parse.quote_plus('alpha+beta gamma', '+') == 'alpha+beta+gamma'
assert parse.quote('/a b') == '/a%20b'
assert parse.quote_plus('/a b') == '%2Fa+b'
assert parse.quote_plus(b'a b', b'+') == 'a+b'
assert parse.quote_plus(bytearray(b'a b')) == 'a+b'
assert parse.quote('AZaz09-._~', safe='') == 'AZaz09-._~'
assert parse.quote_from_bytes(b'\x00\x7f\x80\xff') == '%00%7F%80%FF'
assert parse.quote_from_bytes(b'\x00\xff', b'\x00\xff') == '\x00%FF'
assert parse.quote('\u00fc', safe='\u00fc', encoding='latin-1') == '%FC'
assert parse.quote('\u00fc', safe=b'\xfc', encoding='latin-1') == '%FC'
assert parse.quote('\u6f22\u5b57') == '%E6%BC%A2%E5%AD%97'
assert parse.quote_plus('\u00a2\u00d8 \u00ff', encoding='latin-1') == '%A2%D8+%FF'
assert parse.quote('\U0001f600') == '%F0%9F%98%80'
assert parse.quote_from_bytes(b'a/b', [47, 255, 300]) == 'a/b'
assert parse.quote_from_bytes(bs=b'a b', safe=b' ') == 'a b'
assert parse.quote(string='a b', safe='', encoding=None, errors=None) == 'a%20b'
assert parse.quote_plus(string='a b', safe=b'') == 'a+b'
assert parse.unquote_to_bytes(string='%ff') == b'\xff'

values = []
for value in range(256):
    values.append(value)
raw = bytes(values)
assert parse.unquote_to_bytes(parse.quote_from_bytes(raw, '')) == raw
assert parse.unquote_to_bytes(bytearray(b'a b')) == b'a b'
mutable = bytearray(b'abc')
quoted = parse.quote_from_bytes(mutable)
unquoted = parse.unquote_to_bytes(mutable)
mutable[0] = 122
assert quoted == 'abc'
assert unquoted == b'abc'
for escaped in (b'a%20b', b'%', b'%x', b'%GG'):
    try:
        parse.unquote_to_bytes(bytearray(escaped))
        assert False
    except TypeError:
        pass
assert parse.unquote_to_bytes('\u6f22%C3%BC') == b'\xe6\xbc\xa2\xc3\xbc'
assert parse.unquote_to_bytes('a+b') == b'a+b'
for malformed in (b'%', b'%0', b'%GG', b'%x1', b'%1x', b'%%41', b'%A%41', b'%\xff0'):
    # Only a valid triplet is consumed; a percent can begin a later triplet.
    expected = malformed
    if malformed == b'%%41':
        expected = b'%A'
    if malformed == b'%A%41':
        expected = b'%AA'
    assert parse.unquote_to_bytes(malformed) == expected
assert parse.unquote_to_bytes('%2520%00%fF') == b'%20\x00\xff'
unchanged = b'abc+\xff'
assert parse.unquote_to_bytes(unchanged) is unchanged
for empty in ('', b'', bytearray()):
    assert parse.unquote_to_bytes(empty) == b''
    assert parse.quote(empty, safe=None) == ''
assert parse.quote('', safe=None, encoding=42, errors=42) == ''
assert parse.quote_from_bytes(b'', None) == ''

for f in (parse.quote_from_bytes, parse.quote, parse.quote_plus, parse.unquote_to_bytes):
    try:
        f()
        assert False
    except TypeError:
        pass
    try:
        f(b'a', unexpected=True)
        assert False
    except TypeError:
        pass
for f in (parse.quote, parse.quote_plus):
    for option in ('encoding', 'errors'):
        try:
            if option == 'encoding':
                f(b'', encoding='utf-8')
            else:
                f(b'', errors='strict')
            assert False
        except TypeError:
            pass
    try:
        f('\u6f22', encoding='latin-1')
        assert False
    except UnicodeEncodeError:
        pass
try:
    parse.quote_from_bytes('abc')
    assert False
except TypeError:
    pass
try:
    parse.quote_from_bytes(b'a', [-1])
    assert False
except ValueError:
    pass
try:
    parse.quote_from_bytes(b'a', None)
    assert False
except TypeError:
    pass
for invalid in (None, 1, []):
    try:
        parse.unquote_to_bytes(invalid)
        assert False
    except AttributeError:
        pass
