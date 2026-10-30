# Agreed project limits, deliberately separate from CPython differential tests.
a = bytearray(b'abc')
calls = []
class Index:
    def __index__(self):
        calls.append('index')
        a.clear()
        return 97
    def __bytes__(self):
        calls.append('bytes')
        a.clear()
        return b'a'
x = Index()
for name in ('find', 'rfind', 'index', 'rindex', 'count', 'startswith', 'endswith'):
    method = getattr(bytearray, name)
    for args in ((x,), (b'a', x), (b'a', 0, x), ((x,),)):
        try:
            method(a, *args)
        except TypeError:
            pass
        else:
            raise AssertionError
        assert calls == [] and a == b'abc'
