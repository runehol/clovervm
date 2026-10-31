# Independently written errors, checked against CPython 3.14.10.
import base64


def fails(data, message):
    try:
        base64.b85decode(data)
    except ValueError as error:
        assert error.__class__ is ValueError
        assert str(error) == message
    else:
        raise AssertionError('missing ValueError')


for case in ((b'~', 'base85 overflow in hunk starting at byte 0'),
                      (b'|NsC1', 'base85 overflow in hunk starting at byte 0'),
                      (b'~~~~~', 'base85 overflow in hunk starting at byte 0'),
                      (b'00000~~~~~', 'base85 overflow in hunk starting at byte 5'),
                      (b'~~~~ ', 'bad base85 character at position 4'),
                      (b'00000~~~~ ', 'bad base85 character at position 9'),
                      (b'~~~~~ ', 'base85 overflow in hunk starting at byte 0')):
    fails(case[0], case[1])
    fails(bytearray(case[0]), case[1])
for pos in range(12):
    for ch in (0, 9, 10, 13, 32, 34, 39, 44, 46, 47, 58, 91, 92, 93, 127, 128, 255):
        values = []
        for i in range(12):
            values.append(ch if i == pos else 48)
        data = bytes(values)
        fails(data, 'bad base85 character at position ' + str(pos))
fails(chr(233), 'string argument should contain only ASCII characters')
for call in (base64.b85encode, base64.b85decode):
    for bad in (None, 42, [], [48], (), {}, 1.0):
        try:
            call(bad)
        except TypeError:
            pass
        else:
            raise AssertionError('missing TypeError')
    try:
        call()
    except TypeError:
        pass
    else:
        raise AssertionError('missing argument')
    try:
        call(s=b'')
    except TypeError:
        pass
    else:
        raise AssertionError('unexpected keyword')
try:
    base64.b85encode('abc')
except TypeError:
    pass
else:
    raise AssertionError('encoding accepts no text')
try:
    base64.b85encode(b'', False, True)
except TypeError:
    pass
else:
    raise AssertionError('too many encoding arguments')
try:
    base64.b85decode(b'', False)
except TypeError:
    pass
else:
    raise AssertionError('too many decoding arguments')
try:
    base64.b85decode(b'', pad=True)
except TypeError:
    pass
else:
    raise AssertionError('decode has no pad option')
