class I(int):
    def __int__(self):
        raise AssertionError

    def __index__(self):
        raise AssertionError


class J(I):
    pass


for value in (0, 1, -1, 2**58 - 1, -(2**58), 2**58, 2**1000, -(2**1000)):
    for cls in (int, I, J):
        integer = cls(value)
        assert integer.__trunc__() == value
        assert integer.__floor__() == value
        assert integer.__ceil__() == value
        assert integer.__trunc__().__class__ is int
        assert integer.__floor__().__class__ is int
        assert integer.__ceil__().__class__ is int
        for method in (int.__trunc__, int.__floor__, int.__ceil__):
            result = method(integer)
            assert result == value
            assert result.__class__ is int
            if cls is int:
                assert result is integer

for method in (int.__trunc__, int.__floor__, int.__ceil__):
    assert method(True) == 1
    assert method(False) == 0
    assert method(True).__class__ is int
    assert method(False).__class__ is int
    for value in (None, 1.0, "1", b"1", [], {}):
        raised = False
        try:
            method(value)
        except TypeError:
            raised = True
        assert raised
    raised = False
    try:
        method()
    except TypeError:
        raised = True
    assert raised
    raised = False
    try:
        method(1, 2)
    except TypeError:
        raised = True
    assert raised


class Override(int):
    def __trunc__(self):
        return 10

    def __floor__(self):
        return 20

    def __ceil__(self):
        return 30


value = Override(-5)
assert value.__trunc__() == 10
assert value.__floor__() == 20
assert value.__ceil__() == 30
assert int.__trunc__(value) == -5
assert int.__floor__(value) == -5
assert int.__ceil__(value) == -5
