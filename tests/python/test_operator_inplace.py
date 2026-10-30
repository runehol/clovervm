# Original operator wrapper regressions, checked against CPython 3.14.5.
# Custom mutation avoids depending on builtin list in-place methods.
import operator


operations = (
    (operator.iadd, operator.__iadd__, "iadd", "add", "radd"),
    (operator.isub, operator.__isub__, "isub", "sub", "rsub"),
    (operator.imul, operator.__imul__, "imul", "mul", "rmul"),
    (operator.imatmul, operator.__imatmul__, "imatmul", "matmul", "rmatmul"),
    (operator.itruediv, operator.__itruediv__, "itruediv", "truediv", "rtruediv"),
    (operator.ifloordiv, operator.__ifloordiv__, "ifloordiv", "floordiv", "rfloordiv"),
    (operator.imod, operator.__imod__, "imod", "mod", "rmod"),
    (operator.ipow, operator.__ipow__, "ipow", "pow", "rpow"),
    (operator.ilshift, operator.__ilshift__, "ilshift", "lshift", "rlshift"),
    (operator.irshift, operator.__irshift__, "irshift", "rshift", "rrshift"),
    (operator.iand, operator.__iand__, "iand", "and", "rand"),
    (operator.ixor, operator.__ixor__, "ixor", "xor", "rxor"),
    (operator.ior, operator.__ior__, "ior", "or", "ror"),
)

events = []
replacement = ["replacement"]
failure = ValueError("in-place failure")


def mutate(self, other):
    self.value += other
    events.append("inplace")
    return self


def replace(self, other):
    return replacement


def return_none(self, other):
    return None


def fresh_result(self, other):
    return [other]


def decline(self, other):
    events.append("decline")
    return NotImplemented


def ordinary(self, other):
    events.append("ordinary")
    return "ordinary"


def reflected(self, other):
    events.append("reflected")
    return "reflected"


def fail(self, other):
    events.append("fail")
    raise failure


def sometimes(self, other):
    self.value += 1
    if self.value < 40:
        return NotImplemented
    return replacement


def check_custom_operations():
    for case in operations:
        operation = case[0]
        alias = case[1]
        iname = case[2]
        name = case[3]
        rname = case[4]
        assert operation is alias
        iname = "__" + iname + "__"
        name = "__" + name + "__"
        rname = "__" + rname + "__"

        class Box:
            def __init__(self):
                self.value = 0

        setattr(Box, iname, mutate)
        a = Box()
        original = a
        events.clear()
        for i in range(40):
            assert operation(a, 1) is original
            assert alias(a, 1) is original
        assert original.value == 80
        assert len(events) == 80

        # Replace a hot in-place method and accept arbitrary successful results.
        setattr(Box, iname, replace)
        assert operation(a, 1) is replacement
        assert a is original
        setattr(Box, iname, return_none)
        assert operation(a, 1) is None
        setattr(Box, iname, fresh_result)
        token = Box()
        result = operation(a, token)
        assert result.__class__ is list
        assert result[0] is token

        # A negative in-place lookup and inherited lookup must invalidate too.
        delattr(Box, iname)
        setattr(Box, name, ordinary)
        for i in range(40):
            assert operation(a, 1) == "ordinary"
        class Child(Box):
            pass
        child = Child()
        assert operation(child, 1) == "ordinary"
        setattr(Box, iname, replace)
        assert operation(a, 1) is replacement
        assert operation(child, 1) is replacement
        setattr(Box, iname, mutate)
        assert operation(child, 2) is child
        assert child.value == 2

        # In-place wins before a strict subclass's reflected operation.
        class Right(Box):
            pass
        setattr(Right, rname, reflected)
        right = Right()
        events.clear()
        assert operation(a, 1) is a
        assert events == ["inplace"]
        setattr(Box, iname, replace)
        assert operation(a, right) is replacement
        setattr(Box, iname, decline)
        for i in range(40):
            events.clear()
            assert operation(a, right) == "reflected"
            assert events == ["decline", "reflected"]
        setattr(Right, rname, decline)
        events.clear()
        assert operation(a, right) == "ordinary"
        assert events == ["decline", "decline", "ordinary"]
        delattr(Box, iname)
        setattr(Right, rname, reflected)
        events.clear()
        assert operation(a, right) == "reflected"
        assert events == ["reflected"]

        # Exceptions stop fallback, retain their identity, and allow later calls.
        setattr(Box, iname, fail)
        events.clear()
        try:
            operation(a, right)
            assert False
        except ValueError as error:
            assert error is failure
        assert events == ["fail"]
        setattr(Box, iname, decline)
        setattr(Right, rname, fail)
        events.clear()
        try:
            operation(a, right)
            assert False
        except ValueError as error:
            assert error is failure
        assert events == ["decline", "fail"]
        setattr(Box, name, fail)
        events.clear()
        try:
            operation(a, 1)
            assert False
        except ValueError as error:
            assert error is failure
        assert events == ["decline", "fail"]
        setattr(Box, iname, replace)
        assert alias(a, right) is replacement

        # A cached fallback must still invoke a previously declining method.
        a.value = 0
        setattr(Box, name, ordinary)
        setattr(Box, iname, sometimes)
        for i in range(39):
            assert operation(a, 1) == "ordinary"
        assert operation(a, 1) is replacement
        assert a.value == 40

        for args in ((), (a,), (a, 1, 2)):
            try:
                operation(*args)
                assert False
            except TypeError:
                pass


check_custom_operations()

for case in (
    (operator.iadd, 2**62 - 1, 1, 2**62),
    (operator.isub, -(2**62), 1, -(2**62) - 1),
    (operator.imul, 2**40, 2**40, 2**80),
    (operator.itruediv, 3, 2, 1.5),
    (operator.ifloordiv, -7, 2, -4),
    (operator.imod, -7, 2, 1),
    (operator.ipow, 2, 70, 2**70),
    (operator.ilshift, 1, 70, 2**70),
    (operator.irshift, 2**70, 69, 2),
    (operator.iand, 6, 3, 2),
    (operator.ixor, 6, 3, 5),
    (operator.ior, 6, 3, 7),
    (operator.iadd, 1.5, 2, 3.5),
    (operator.iadd, (1,), (2,), (1, 2)),
    (operator.iadd, "a", "b", "ab"),
):
    operation = case[0]
    a = case[1]
    b = case[2]
    expected = case[3]
    original = a
    assert operation(a, b) == expected
    assert a is original

for operation in (operator.itruediv, operator.ifloordiv, operator.imod):
    try:
        operation(1, 0)
        assert False
    except ZeroDivisionError:
        pass

for case in operations:
    operation = case[0]
    try:
        operation(None, None)
        assert False
    except TypeError:
        pass
