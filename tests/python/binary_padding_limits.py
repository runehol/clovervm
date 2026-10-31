# VM-specific storage/index limits, deliberately separate from the CPython oracle.
class PaddingIndex:
    def __index__(self):
        raise AssertionError('custom index protocol is excluded')

for cls in (bytes, bytearray):
    for name in ('ljust', 'rjust', 'center', 'zfill'):
        method = getattr(cls, name)
        for width in (288230376151711744, 9223372036854775807):
            raised = False
            try:
                method(cls(b'abc'), width)
            except OverflowError:
                raised = True
            assert raised
        raised = False
        try:
            method(cls(b'abc'), PaddingIndex())
        except TypeError:
            raised = True
        assert raised
    for name in ('ljust', 'rjust', 'center'):
        method = getattr(cls, name)
        # Fill validation precedes positive result-storage limits.
        raised = False
        try:
            method(cls(b'abc'), 9223372036854775807, b'')
        except TypeError:
            raised = True
        assert raised
