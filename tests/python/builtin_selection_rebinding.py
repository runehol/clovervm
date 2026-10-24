# Private bootstrap dependencies must survive public builtin rebinding, including
# callbacks after call/global caches have warmed.
import builtins

Sorted = sorted
Min = min
Max = max
List = list
Len = len
Iter = iter
Next = next
Str = str
Isinstance = isinstance
TypeErrorClass = TypeError
ValueErrorClass = ValueError
Stop = StopIteration
calls = []


def poison():
    builtins.sorted = None
    builtins.min = None
    builtins.max = None
    builtins.list = None
    builtins.len = None
    builtins.iter = None
    builtins.next = None
    builtins.str = None
    builtins.isinstance = None
    builtins.TypeError = None
    builtins.ValueError = None


def restore():
    builtins.sorted = Sorted
    builtins.min = Min
    builtins.max = Max
    builtins.list = List
    builtins.len = Len
    builtins.iter = Iter
    builtins.next = Next
    builtins.str = Str
    builtins.isinstance = Isinstance
    builtins.TypeError = TypeErrorClass
    builtins.ValueError = ValueErrorClass


def key(item):
    poison()
    calls.append(item)
    return -item


class Source:
    def __init__(self):
        self.index = 0

    def __iter__(self):
        poison()
        return self

    def __next__(self):
        if self.index == 3:
            raise Stop
        self.index += 1
        return self.index


# Warm both ordinary and keyword calls before replacing public dependencies.
for repeat in range(3):
    assert sorted is Sorted and min is Min and max is Max
    assert Sorted([3, 1, 2]) == [1, 2, 3]
    assert Sorted([3, 1, 2], reverse=True) == [3, 2, 1]
    assert Min(3, 1, 2) == 1
    assert Max([3, 1, 2], default=None) == 3

for function in (Sorted, Min, Max):
    calls.clear()
    try:
        result = function(Source(), key=key)
        if function is Sorted:
            assert result == [3, 2, 1]
        elif function is Min:
            assert result == 3
        else:
            assert result == 1
        assert calls == [1, 2, 3]
        assert sorted is None and min is None and max is None
        assert Sorted([2, 1]) == [1, 2]
        assert Min(2, 1) == 1
        assert Max(2, 1) == 2
        assert Min([], default=None) is None
        assert Max([], default=None) is None
        try:
            Min([])
        except ValueErrorClass:
            pass
        else:
            raise AssertionError("expected ValueError")
        try:
            Max(1, 2, default=None)
        except TypeErrorClass:
            pass
        else:
            raise AssertionError("expected TypeError")
        try:
            Sorted()
        except TypeErrorClass:
            pass
        else:
            raise AssertionError("expected TypeError")
    finally:
        restore()
