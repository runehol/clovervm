# bytes(count) behavior shared with CPython, plus Clover's storage limit.
import sys


class B(bytes):
    pass


class C(B):
    pass


class I(int):
    def __index__(self):
        raise AssertionError


for cls in (bytes, B, C):
    for count in (0, 1, 5, 65536, False, True, I(0), I(3)):
        result = cls(count)
        assert result.__class__ is cls
        assert len(result) == count
        assert result.count(0) == count
        direct = bytes.__new__(cls, count)
        assert direct.__class__ is cls
        assert direct == result
        if cls is not bytes:
            result.extra = 42
            assert result.__dict__["extra"] == 42
            another = cls(count)
            assert another is not result
            assert not hasattr(another, "extra")

    # Signed-64-bit conversion precedes the negativity check. Negative counts
    # outside SMI range must still raise ValueError when they fit int64.
    for count in (-1, -(2**58), -(2**63), I(-2)):
        raised = False
        try:
            cls(count)
        except ValueError:
            raised = True
        assert raised

    for count in (2**63, -(2**63) - 1, 10**100, -(10**100)):
        raised = False
        try:
            cls(count)
        except OverflowError:
            raised = True
        assert raised

    raised = False
    try:
        cls(1.5)
    except TypeError:
        raised = True
    assert raised

    # Clover stores lengths as SMIs; reject these before attempting allocation.
    if sys.implementation.name == "clovervm":
        for count in (2**58, 2**63 - 1):
            raised = False
            try:
                cls(count)
            except OverflowError:
                raised = True
            assert raised
