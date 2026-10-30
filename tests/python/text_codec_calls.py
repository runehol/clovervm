# Strict-only policy is the agreed initial slice.
def raises(cls, fn, *args, **kwargs):
    try:
        fn(*args, **kwargs)
    except cls:
        return
    assert False

for fn in (str.encode, bytes.decode):
    obj = 'abc' if fn is str.encode else b'abc'
    raises(TypeError, fn)
    raises(TypeError, fn, obj, 'utf8', 'strict', 'extra')
    raises(TypeError, fn, obj, unknown=1)
    raises(TypeError, fn, obj, encoding=None)
    raises(TypeError, fn, obj, errors=None)
    raises(TypeError, fn, obj, 'utf8', encoding='latin1')
    raises(TypeError, fn, obj, 'not-a-codec', errors=None)
    raises(ValueError, fn, obj, 'utf-8\0other')
    raises(ValueError, fn, obj, errors='strict\0other')
    raises(LookupError, fn, obj, 'not-a-codec')
    raises(LookupError, fn, obj, 'utf.8')
    raises(NotImplementedError, fn, obj, errors='ignore')
    raises(NotImplementedError, fn, obj, errors='replace')
    raises(NotImplementedError, fn, obj, errors='missing-handler')

raises(TypeError, str.encode, b'abc')
raises(TypeError, bytes.decode, 'abc')
raises(TypeError, str.encode, self='abc')
raises(TypeError, bytes.decode, self=b'abc')
raises(LookupError, str.encode, '', 'not-a-codec')
raises(TypeError, bytes.decode, b'', 'not-a-codec', errors=None)
raises(NotImplementedError, bytes.decode, b'', errors='ignore')
