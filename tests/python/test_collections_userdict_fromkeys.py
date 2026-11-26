# Self-checking UserDict.fromkeys behavior on CloverVM and CPython 3.14.10.
from collections import UserDict


def raises(exception_type, call, *args, **kwargs):
    try:
        call(*args, **kwargs)
    except exception_type as error:
        return error
    assert False, "expected an exception"


result = UserDict.fromkeys("aba")
assert result.__class__ is UserDict
assert list(result.data) == ["a", "b"]
assert result.data == {"a": None, "b": None}
assert UserDict.fromkeys([]).data == {}
assert UserDict.fromkeys({"a": 42}, 1).data == {"a": 1}
assert UserDict.fromkeys(iterable="x", value=2).data == {"x": 2}
assert UserDict.fromkeys(**{"iterable": "x", "value": 3}).data == {"x": 3}

raises(TypeError, UserDict.fromkeys)
raises(TypeError, UserDict.fromkeys, "x", 1, 2)
raises(TypeError, UserDict.fromkeys, "x", iterable="y")
raises(TypeError, UserDict.fromkeys, "x", 1, value=2)
raises(TypeError, UserDict.fromkeys, "x", cls=UserDict)
raises(TypeError, UserDict.fromkeys, "x", keys="y")
raises(TypeError, UserDict.fromkeys, 42)
raises(TypeError, UserDict.fromkeys, [[]])


events = []


class RecordingDict(UserDict):
    def __init__(self):
        events.append("init")
        UserDict.__init__(self)

    def __setitem__(self, key, value):
        events.append(("set", key))
        UserDict.__setitem__(self, key, value)


class Keys:
    def __iter__(self):
        events.append("iter")
        self.keys = iter("aba")
        return self

    def __next__(self):
        events.append("next")
        return next(self.keys)


value = []
result = RecordingDict.fromkeys(Keys(), value=value)
assert result.__class__ is RecordingDict
assert list(result.data) == ["a", "b"]
assert result.data["a"] is value and result.data["b"] is value
assert events == [
    "init", "iter", "next", ("set", "a"), "next", ("set", "b"),
    "next", ("set", "a"), "next",
]

instance = RecordingDict()
instance["old"] = 1
result = instance.fromkeys("x")
assert result is not instance and result.__class__ is RecordingDict
assert result.data == {"x": None}
assert instance.data == {"old": 1}
saved = instance.fromkeys
assert saved(iterable="y").data == {"y": None}


failure = ValueError("callback failed")
stop = StopIteration("callback raised StopIteration")


class FailingConstructor(RecordingDict):
    def __init__(self):
        events.append("failing init")
        raise failure


class FailingIter:
    def __iter__(self):
        raise stop


class FailingNext(Keys):
    def __next__(self):
        raise failure


class FailingAssignment(RecordingDict):
    def __setitem__(self, key, value):
        events.append(("failed set", key))
        raise stop


events.clear()
assert raises(ValueError, FailingConstructor.fromkeys, Keys()) is failure
assert events == ["failing init"]
assert raises(StopIteration, UserDict.fromkeys, FailingIter()) is stop
assert raises(ValueError, UserDict.fromkeys, FailingNext()) is failure
events.clear()
assert raises(StopIteration, FailingAssignment.fromkeys, Keys()) is stop
assert events == ["init", "iter", "next", ("failed set", "a")]
