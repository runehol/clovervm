# Original regressions, checked against CPython 3.14.5. The managed constructor
# must not resolve its validator, errors, or truth conversion via public names.
import builtins

Bool = bool
Len = len
TypeErrorClass = TypeError
ValueErrorClass = ValueError
calls = []


def poison():
    builtins.bool = None
    builtins.len = None
    builtins.TypeError = None
    builtins.ValueError = None


class Truth:
    def __bool__(self):
        poison()
        calls.append(1)
        return True


class Length:
    def __len__(self):
        poison()
        calls.append(2)
        return 0


class BadBool:
    def __bool__(self):
        poison()
        return 1


class BadLength:
    def __len__(self):
        poison()
        return -1


for repeat in range(4):
    assert Bool(Truth()) is True
    assert Bool(Length()) is False
try:
    assert bool is None
    try:
        bool(Truth())
    except TypeErrorClass:
        pass
    else:
        assert False
    assert Bool() is False
    assert Bool.__new__(Bool, Truth()) is True
    assert Bool.__new__(Bool, Length()) is False
    try:
        Bool(BadBool())
    except TypeErrorClass:
        pass
    else:
        assert False
    try:
        Bool(BadLength())
    except ValueErrorClass:
        pass
    else:
        assert False
    try:
        Bool.__new__(int, Truth())
    except TypeErrorClass:
        pass
    else:
        assert False
    assert calls == [1, 2, 1, 2, 1, 2, 1, 2, 1, 2]
finally:
    builtins.bool = Bool
    builtins.len = Len
    builtins.TypeError = TypeErrorClass
    builtins.ValueError = ValueErrorClass
