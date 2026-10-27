# Original diagnostic regressions checked against CPython 3.14.5.
for case in ((b'\xff', 0, 1, 'invalid start byte'),
             (b'a\xe2\x82!', 1, 3, 'invalid continuation byte'),
             (b'a\xe2\x82', 1, 3, 'unexpected end of data'),
             (b'\xed\xa0', 0, 1, 'invalid continuation byte'),
             (b'\xed\xa0\x80', 0, 1, 'invalid continuation byte'),
             (b'\xe0\x80\x80', 0, 1, 'invalid continuation byte'),
             (b'\xf4\x90\x80\x80', 0, 1, 'invalid continuation byte'),
             (b'ok\xf0\x9f\x98', 2, 5, 'unexpected end of data')):
    data = case[0]
    try:
        data.decode('UTF8')
        assert False
    except UnicodeDecodeError as e:
        assert e.encoding == 'utf-8' and e.object == data
        assert e.start == case[1] and e.end == case[2]
        assert e.reason == case[3]
        assert e.args == ('utf-8', data, case[1], case[2], case[3])
        assert str(e).endswith(': ' + case[3])

for case in (('a\ud800\udfffX', 'UTF8', 1, 3, 'utf-8', 'surrogates not allowed'),
             ('a\u0100\ud800X', 'latin1', 1, 3, 'latin-1', 'ordinal not in range(256)'),
             ('\ud800', 'utf-8', 0, 1, 'utf-8', 'surrogates not allowed')):
    text = case[0]
    try:
        text.encode(case[1])
        assert False
    except UnicodeEncodeError as e:
        assert e.object is text and e.encoding == case[4]
        assert e.start == case[2] and e.end == case[3]
        assert e.reason == case[5]
        assert e.args == (case[4], text, case[2], case[3], case[5])
        assert str(e).endswith(': ' + case[5])

class Text(str):
    pass

text = Text('x\ud800')
try:
    text.encode()
    assert False
except UnicodeEncodeError as e:
    assert e.object is text and e.start == 1

# Existing objects stay usable after the native error path.
assert 'recovered'.encode() == b'recovered'
assert b'recovered'.decode() == 'recovered'
