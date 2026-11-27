import collections
from collections import ChainMap, UserDict, deque


def check_raises(kind, action, /, *args, **kwargs):
    try:
        action(*args, **kwargs)
    except kind as error:
        return error
    assert False


def test_ends():
    queue = deque()
    assert not queue
    assert queue.maxlen is None
    assert str(check_raises(IndexError, queue.pop)) == "pop from an empty deque"
    assert str(check_raises(IndexError, queue.popleft)) == "pop from an empty deque"

    first = object()
    second = object()
    assert queue.append(first) is None
    assert queue.appendleft(second) is None
    assert queue and len(queue) == 2
    assert queue.pop() is first
    assert queue.popleft() is second

    queue.extend(range(4))
    assert queue.popleft() == 0
    assert queue.popleft() == 1
    queue.extend([4, 5])
    queue.appendleft(1)
    queue.extend(range(6, 40))
    assert list(queue) == list(range(1, 40))
    for right in range(39, 20, -1):
        assert queue.pop() == right
    for left in range(1, 21):
        assert queue.popleft() == left
    assert not queue


def test_bounded():
    assert list(deque("abc", 2)) == ["b", "c"]
    queue = deque(iterable=range(9), maxlen=3)
    assert list(queue) == [6, 7, 8]
    queue.append(None)
    assert list(queue) == [7, 8, None]
    queue.appendleft(6)
    assert list(queue) == [6, 7, 8]
    assert queue.extend([9, 10]) is None
    assert list(queue) == [8, 9, 10]
    assert queue.extendleft([7, 6]) is None
    assert list(queue) == [6, 7, 8]
    for step in range(40):
        queue.append(step)
    assert list(queue) == [37, 38, 39]
    for step in range(40):
        queue.appendleft(step)
    assert list(queue) == [39, 38, 37]

    queue = deque(maxlen=1)
    for step in range(20):
        queue.append(step)
        queue.appendleft(None)
        assert len(queue) == 1
        assert queue.pop() is None


class PayloadInt(int):
    def __index__(self):
        raise AssertionError

    def __int__(self):
        raise AssertionError


def test_binding_and_limit():
    assert deque(maxlen=True).maxlen == 1
    assert deque(maxlen=False).maxlen == 0
    assert deque(maxlen=PayloadInt(2**63 - 1)).maxlen == 2**63 - 1
    check_raises(OverflowError, deque, maxlen=2**63)
    check_raises(OverflowError, deque, maxlen=-(2**63) - 1)
    assert str(check_raises(ValueError, deque, None, -1)) == "maxlen must be non-negative"
    check_raises(TypeError, deque, maxlen=1.0)
    check_raises(TypeError, deque, None)
    check_raises(TypeError, deque, [], None, None)
    check_raises(TypeError, deque, [], iterable=[])

    queue = deque()
    check_raises(TypeError, queue.append, item=1)
    check_raises(TypeError, queue.appendleft)
    check_raises(TypeError, queue.extend, iterable=[])
    check_raises(TypeError, queue.extendleft, [], [])
    check_raises(TypeError, queue.pop, 1)
    check_raises(TypeError, queue.popleft, queue=queue)
    check_raises(AttributeError, setattr, queue, "maxlen", 1)
    check_raises(AttributeError, delattr, queue, "maxlen")
    check_raises(TypeError, hash, queue)


def check_mutated(iterator):
    for attempt in range(2):
        error = check_raises(RuntimeError, next, iterator)
        assert str(error) == "deque mutated during iteration"


def test_iterators_and_aliases():
    queue = deque([1, 2, 3], 3)
    iterator = iter(queue)
    assert iter(iterator) is iterator
    assert next(iterator) == 1
    queue.append(4)
    check_mutated(iterator)

    queue = deque([1, 2, 3], 3)
    queue.extend(queue)
    assert list(queue) == [1, 2, 3]
    queue.extendleft(queue)
    assert list(queue) == [3, 2, 1]
    check_raises(RuntimeError, queue.extend, iter(queue))
    assert list(queue) == [2, 1, 3]
    check_raises(RuntimeError, queue.extendleft, iter(queue))
    assert list(queue) == [2, 2, 1]

    queue = deque([1, 2])
    queue.extend(queue)
    assert list(queue) == [1, 2, 1, 2]
    iterator = iter(queue)
    assert list(iterator) == [1, 2, 1, 2]
    check_raises(StopIteration, next, iterator)
    queue.pop()
    check_mutated(iterator)

    queue = deque([None])
    iterator = iter(queue)
    del queue
    assert next(iterator) is None
    check_raises(StopIteration, next, iterator)


class FailingSource:
    def __init__(self, target):
        self.target = target
        self.events = []

    def __iter__(self):
        self.events.append("iter")
        return self

    def __next__(self):
        self.events.append("next")
        if len(self.events) == 2:
            return "yielded"
        self.events.append(list(self.target))
        raise LookupError("failed")


class StopsDuringAcquisition:
    def __iter__(self):
        raise StopIteration("acquisition")


class ReinitializingSource:
    def __init__(self, target, limit):
        self.target = target
        self.limit = limit

    def __iter__(self):
        self.target.__init__(maxlen=self.limit)
        return iter([1, 2, 3])


class MutatingSource:
    def __init__(self, target):
        self.target = target
        self.yielded = False

    def __iter__(self):
        return self

    def __next__(self):
        if self.yielded:
            raise StopIteration
        self.yielded = True
        self.target.append("callback")
        return "yielded"


def test_callbacks():
    queue = deque(["original"], 2)
    source = FailingSource(queue)
    assert str(check_raises(LookupError, queue.extendleft, source)) == "failed"
    assert list(queue) == ["yielded", "original"]
    assert source.events == ["iter", "next", "next", ["yielded", "original"]]
    check_raises(StopIteration, queue.extend, StopsDuringAcquisition())
    assert list(queue) == ["yielded", "original"]
    queue.extend(MutatingSource(queue))
    assert list(queue) == ["callback", "yielded"]

    queue = deque(maxlen=0)
    iterator = iter(queue)
    source = FailingSource(queue)
    assert str(check_raises(LookupError, queue.extend, source)) == "failed"
    assert source.events == ["iter", "next", "next", []]
    values = iter([1, 2])
    queue.extendleft(values)
    assert list(values) == []
    check_raises(StopIteration, next, iterator)
    queue.appendleft(1)
    check_mutated(iterator)
    assert not queue

    queue.extend(ReinitializingSource(queue, 2))
    assert queue.maxlen == 2 and list(queue) == []
    queue.extend(ReinitializingSource(queue, 0))
    assert queue.maxlen == 0 and list(queue) == [2, 3]
    queue.append(4)
    assert list(queue) == [3, 4]


def test_reinitialization_and_composition():
    queue = deque([1])
    iterator = iter(queue)
    source = FailingSource(queue)
    check_raises(ValueError, queue.__init__, source, -1)
    assert source.events == [] and next(iterator) == 1
    check_raises(LookupError, queue.__init__, source, 1)
    assert queue.maxlen == 1 and list(queue) == ["yielded"]
    assert source.events == ["iter", "next", "next", ["yielded"]]
    check_mutated(iterator)
    check_raises(TypeError, queue.__init__, None, 2)
    assert queue.maxlen == 2 and not queue
    iterator = iter(queue)
    queue.__init__(maxlen=1)
    check_raises(StopIteration, next, iterator)
    queue.append(1)
    queue.__init__(queue, 4)
    assert queue.maxlen == 4 and not queue

    queue.extend(ChainMap(UserDict(a=1), {"b": 2}))
    assert list(queue) == ["b", "a"]
    result = UserDict.fromkeys(queue, 3)
    assert list(result) == ["b", "a"]
    assert result["a"] == result["b"] == 3


class TrackedDeque(deque):
    def __init__(self, values):
        self._size = 0
        self._slots = "subclass slots"
        self._head = "subclass head"
        self._maxlen = "subclass maxlen"
        self._version = "subclass version"
        deque.__init__(self, values, maxlen=3)


def test_subclass_state():
    queue = TrackedDeque([1, 2])
    queue._size = 10
    queue.append(3)
    queue.append(4)
    assert list(queue) == [2, 3, 4] and queue.maxlen == 3
    assert queue._size == 10
    assert queue._slots == "subclass slots"
    assert queue._head == "subclass head"
    assert queue._maxlen == "subclass maxlen"
    assert queue._version == "subclass version"


def test_rebound_export():
    queue = deque([1, 2, 3], 5)
    collections.deque = None
    try:
        queue.append(4)
        queue.appendleft(0)
        queue.extend([5, 6])
        queue.extendleft([1])
        assert queue.pop() == 5
        assert queue.popleft() == 1
        assert list(queue) == [2, 3, 4]
        assert list(deque([1])) == [1]
    finally:
        collections.deque = deque


test_ends()
test_bounded()
test_binding_and_limit()
test_iterators_and_aliases()
test_callbacks()
test_reinitialization_and_composition()
test_subclass_state()
test_rebound_export()
