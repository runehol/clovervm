# Deliberate ordinary-attribute semantics, unlike CPython's typed descriptors.
e = UnicodeDecodeError('utf-8', b'abc', 0, 1, 'why')
original = e.args
for name in ('encoding', 'object', 'start', 'end', 'reason'):
    setattr(e, name, ['ordinary', name])
    assert getattr(e, name) == ['ordinary', name]
    delattr(e, name)
    assert not hasattr(e, name)
    setattr(e, name, None)
    assert getattr(e, name) is None
assert e.args == original

# Invalid positional attributes are rejected safely at formatting time.
e = UnicodeEncodeError('x', 'abc', 0, 1, 'why')
e.start = 'bad'
try:
    str(e)
    assert False
except TypeError:
    pass

x = UnicodeDecodeError('utf-8', b'abc', 0, 1, 'saved')

# Field changes do not rewrite args; the inherited initializer can update args
# independently (the existing args attribute itself is read-only in CloverVM).
x.reason = 'changed'
assert x.args[-1] == 'saved'
BaseException.__init__(x, 'new args')
assert x.args == ('new args',) and x.reason == 'changed'

# Constructors retain every supplied object without invoking conversions.
class NoConversion:
    def __index__(self):
        raise RuntimeError('must not call index')
    def __str__(self):
        raise RuntimeError('must not call str during construction')

for cls in (UnicodeEncodeError, UnicodeDecodeError):
    encoding = NoConversion()
    obj = bytearray(b'abc')
    start = NoConversion()
    end = 2 ** 100
    reason = ['arbitrary reason']
    e = cls(encoding, obj, start, end, reason)
    assert e.encoding is encoding and e.args[0] is encoding
    assert e.object is obj and e.args[1] is obj
    assert e.start is start and e.args[2] is start
    assert e.end is end and e.args[3] is end
    assert e.reason is reason and e.args[4] is reason
    obj[0] = 255
    assert e.object[0] == 255
    # Reinitialization also stores arbitrary values as given.
    cls.__init__(e, None, obj, True, False, reason)
    assert e.encoding is None and e.object is obj
    assert e.start is True and e.end is False
    assert e.args == (None, obj, True, False, reason)
    # Formatting rejects unsuitable fields without conversion callbacks.
    try:
        str(e)
        assert False
    except TypeError:
        pass

# Large positions are accepted at construction; formatting still checks bounds.
e = UnicodeEncodeError('x', 'abc', 2 ** 100, 1, 'why')
assert e.start == 2 ** 100
try:
    str(e)
    assert False
except OverflowError:
    pass
