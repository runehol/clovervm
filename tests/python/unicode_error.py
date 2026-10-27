# Hierarchy, arity, and formatting of valid inputs checked against CPython 3.14.5.
assert issubclass(UnicodeError, ValueError)
assert issubclass(UnicodeEncodeError, UnicodeError)
assert issubclass(UnicodeDecodeError, UnicodeError)
assert UnicodeError('plain').args == ('plain',)
assert str(UnicodeError('plain')) == 'plain'


def raises(cls, fn, *args, **kwargs):
    try:
        fn(*args, **kwargs)
    except cls:
        return
    assert False


for case in ((UnicodeEncodeError, 'abc', 'encode'),
             (UnicodeDecodeError, b'abc', 'decode')):
    cls = case[0]
    obj = case[1]
    action = case[2]
    e = cls('utf-8', obj, 1, 2, 'why')
    assert e.args == ('utf-8', obj, 1, 2, 'why')
    assert e.encoding == 'utf-8' and e.object is obj
    assert e.start == 1 and e.end == 2 and e.reason == 'why'
    if action == 'encode':
        assert str(e) == "'utf-8' codec can't encode character '\\x62' in position 1: why"
    else:
        assert str(e) == "'utf-8' codec can't decode byte 0x62 in position 1: why"
    assert repr(e).startswith(cls.__name__ + '(')
    for bounds in ((0, 0), (3, 4), (2, 1), (-1, 0), (-1, -1),
                       (-(2 ** 63), -(2 ** 63)),
                       (2 ** 63 - 1, 2 ** 63 - 1)):
        start = bounds[0]
        end = bounds[1]
        e = cls('x', obj, start, end, 'why')
        inclusive_end = end - 1
        if end == -(2 ** 63):
            inclusive_end = 2 ** 63 - 1
        noun = 'characters' if action == 'encode' else 'bytes'
        assert str(e) == "'x' codec can't " + action + ' ' + noun + ' in position ' + str(start) + '-' + str(inclusive_end) + ': why'
    raises(TypeError, cls)
    raises(TypeError, cls, 'x', obj, 0, 1)
    raises(TypeError, cls, 'x', obj, 0, 1, 'why', 6)
    raises(TypeError, cls, encoding='x', object=obj, start=0, end=1, reason='why')
    e = cls('x', obj, 0, 1, 'why')
    e.encoding = None
    e.reason = None
    assert str(e).startswith("'None' codec can't ")
    assert str(e).endswith(': None')
    e.object = None
    raises(TypeError, str, e)


for case in (('a', '\\x61'), ('\xff', '\\xff'),
                      ('\u0100', '\\u0100'), ('\ud800', '\\ud800'),
                      ('\U0001f600', '\\U0001f600')):
    char = case[0]
    escaped = case[1]
    e = UnicodeEncodeError('x', char, 0, 1, 'bad')
    assert str(e) == "'x' codec can't encode character '" + escaped + "' in position 0: bad"

class Encode(UnicodeEncodeError):
    pass


class Decode(UnicodeDecodeError):
    pass


for case in ((Encode, 'abc'), (Decode, b'abc')):
    cls = case[0]
    obj = case[1]
    for i in range(30):
        e = cls('x', obj, 0, 1, 'why')
        assert e.object is obj and e.start == 0
        e.extra = ['retained', i]
        assert e.extra[1] == i


class Custom(UnicodeDecodeError):
    def __init__(self, value):
        self.value = value


assert Custom(42).value == 42
raises(TypeError, UnicodeEncodeError.__str__, UnicodeError())
raises(TypeError, UnicodeDecodeError.__init__, UnicodeError(), 'x', b'a', 0, 1, 'bad')

