# Original one-shot codec regressions checked against CPython 3.14.5.
for case in (('', b''), ('abc', b'abc'), ('a\0b', b'a\0b'),
             ('\x80\xff', b'\xc2\x80\xc3\xbf'),
             ('\u0100\u20ac', b'\xc4\x80\xe2\x82\xac'),
             ('\U0001f600\U0010ffff', b'\xf0\x9f\x98\x80\xf4\x8f\xbf\xbf'),
             ('\ufeffX', b'\xef\xbb\xbfX')):
    text = case[0]
    data = case[1]
    assert text.encode() == data
    assert data.decode() == text
    assert str.encode(text, errors='strict', encoding='utf-8') == data
    assert bytes.decode(data, encoding='utf-8', errors='strict') == text

for alias in ('utf8', 'UTF_8', 'utf-8', ' utf 8 ', 'UTF--8', 'u8', 'utf',
              'cp65001', 'utf8_ucs2', 'utf8_ucs4', 'utf\u00e98'):
    assert '\u20ac'.encode(alias) == b'\xe2\x82\xac'
    assert b'\xe2\x82\xac'.decode(alias) == '\u20ac'

for alias in ('latin-1', 'latin1', 'latin_1', '8859', 'cp819', 'csisolatin1',
              'ibm819', 'iso8859', 'iso8859_1', 'iso_8859_1',
              'iso_8859_1_1987', 'iso_ir_100', 'l1', 'latin', 'ISO--8859--1'):
    assert '\0\x80\xff'.encode(alias) == b'\0\x80\xff'
    assert b'\0\x80\xff'.decode(alias) == '\0\x80\xff'

# All byte values, including embedded zeroes, roundtrip through Latin-1.
values = []
for i in range(256):
    values.append(i)
source = bytes(values)
text = source.decode('latin1')
assert len(text) == 256 and ord(text[255]) == 255
assert text.encode('latin1') == source

class Text(str):
    def __str__(self):
        raise RuntimeError('must use stored text')

class Data(bytes):
    pass

class Name(str):
    def lower(self):
        raise RuntimeError('must use stored name')

text = Text('abc')
data = Data(b'abc')
assert str.encode(text, Name('utf-8')) == b'abc'
assert bytes.decode(data, Name('utf-8')) == 'abc'
assert text.encode().__class__ is bytes
assert data.decode().__class__ is str
assert b''.decode('not-a-codec') == ''
