# Independent Ascii85 errors, checked against CPython 3.14.10.
import base64


def fails(data, message, **options):
    try:
        base64.a85decode(data, **options)
    except ValueError as error:
        assert error.__class__ is ValueError
        assert str(error) == message
    else:
        raise AssertionError('missing ValueError')


for data in (b's', b's8', b's8W', b's8W-', b's8W-"', b'uuuuu', b'uu'):
    fails(data, 'Ascii85 overflow')
for data in (b'!z', b'!!!!z'):
    fails(data, 'z inside Ascii85 5-tuple')
fails(b'!y', 'y inside Ascii85 5-tuple', foldspaces=True)
fails(b'y', 'Non-Ascii85 digit found: y')
fails(b'\f', 'Non-Ascii85 digit found: \f')
fails(b'uuuu?', 'Ascii85 overflow')
fails(b'uuuu~', 'Non-Ascii85 digit found: ~')
fails(b'uuuuu~', 'Ascii85 overflow')
for data in (b'', b'<~z', b'<~z~>\n', b'<~z~> '):
    fails(data, "Ascii85 encoded byte sequences must end with b'~>'", adobe=True)
fails(b'<~~>', 'Non-Ascii85 digit found: ~')
fails(chr(233), 'string argument should contain only ASCII characters', adobe=True)
for ignore in (None, 42, ' '):
    try:
        base64.a85decode(b' ', ignorechars=ignore)
    except TypeError:
        pass
    else:
        raise AssertionError('invalid ignorechars used')
for call in (base64.a85encode, base64.a85decode):
    for bad in (None, 42, [], {}, 1.0):
        try:
            call(bad)
        except TypeError:
            pass
        else:
            raise AssertionError('invalid input')
    for case in (((), {}), ((b'', True), {}), ((b'',), {'unknown': True}),
                 ((), {'s': b''})):
        args = case[0]
        kwargs = case[1]
        try:
            call(*args, **kwargs)
        except TypeError:
            pass
        else:
            raise AssertionError('invalid signature accepted')
for width in (1.5, 2.0, 'x'):
    for raw in (b'', b'foo'):
        try:
            base64.a85encode(raw, wrapcol=width)
        except TypeError:
            pass
        else:
            raise AssertionError('invalid wrap width')
# Clamping chooses the integer minimum, including a float tie.
assert base64.a85encode(b'foo', wrapcol=1.0) == b'A\no\nD\nS'
assert base64.a85encode(b'foo', adobe=True, wrapcol=2.0) == b'<~\nAo\nDS\n~>'


class Width(int):
    def __int__(self):
        raise AssertionError('int conversion hook')
    def __index__(self):
        raise AssertionError('index conversion hook')
assert base64.a85encode(b'foo', wrapcol=Width(5)) == b'AoDS'
assert base64.a85encode(b'foo', wrapcol=Width(18446744073709551616)) == b'AoDS'
