# The exception-context scan must skip omitted slice components, while still
# finding raises which occur later in the handler.
def slice_without_raise():
    data = b'abcdef'
    try:
        raise ValueError('caught')
    except ValueError:
        assert data[:] == b'abcdef'
        assert data[1:] == b'bcdef'
        assert data[:3] == b'abc'
        assert data[::2] == b'ace'
        assert data[1::2] == b'bdf'
        assert data[:3:2] == b'ac'
        assert data[1:3] == b'bc'
        assert data[1:3:1] == b'bc'
    return 42


assert slice_without_raise() == 42
# A handled exception must not remain active after leaving the function.
try:
    raise
except RuntimeError:
    pass
else:
    assert False


original = ValueError('original')
try:
    try:
        raise original
    except ValueError:
        data = b'abcdef'[:]
        # Handling another exception must not replace the outer exception
        # saved for the bare raise following this slice.
        try:
            raise TypeError('nested')
        except TypeError:
            assert data[1:] == b'bcdef'
        raise
except ValueError as caught:
    assert caught is original
else:
    assert False


replacement = TypeError('replacement')
try:
    try:
        raise original
    except ValueError:
        assert b'abcdef'[:3] == b'abc'
        raise replacement
except TypeError as caught:
    assert caught is replacement
else:
    assert False
