# Formatting callbacks and retention of the supplied input objects.
def raises(cls, fn, *args, **kwargs):
    try:
        fn(*args, **kwargs)
    except cls:
        return
    assert False


class RaisingText:
    def __str__(self):
        raise RuntimeError('format failure')


for case in ((UnicodeEncodeError, 'abc'), (UnicodeDecodeError, b'abc')):
    cls = case[0]
    obj = case[1]
    e = cls('x', obj, 0, 1, 'why')
    e.reason = RaisingText()
    raises(RuntimeError, str, e)
    e.reason = 'recovered'
    assert str(e).endswith(': recovered')
    e.encoding = RaisingText()
    raises(RuntimeError, str, e)


def retained_decode_error():
    source = b'payload' + bytes([33])
    return UnicodeDecodeError('utf-8', source, 0, 1, 'retained')


def retained_encode_error():
    source = 'payload' + str(321)
    return UnicodeEncodeError('utf-8', source, 0, 1, 'retained')


decode = retained_decode_error()
encode = retained_encode_error()
for i in range(100):
    garbage = ['allocate', i, bytearray([i, 1, 2, 3])]
    assert decode.object == b'payload!'
    assert decode.args[1] is decode.object
    assert encode.object == 'payload321'
    assert encode.args[1] is encode.object
    assert str(decode).endswith(': retained')
    assert str(encode).endswith(': retained')


