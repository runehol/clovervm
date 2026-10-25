# Python test set -- heapq max-heap operations
#
# Portions adapted from CPython's Lib/test/test_heapq.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import heapq


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


for size in range(35):
    data = []
    for i in range(size):
        data.append((i * 17 + size) % 23 - 11)
    expected = sorted(data, reverse=True)
    heap = data.copy()
    assert heapq.heapify_max(heap) is None
    for i in range(1, len(heap)):
        assert not heap[(i - 1) // 2] < heap[i]
    popped = []
    while heap:
        popped.append(heapq.heappop_max(heap))
    assert popped == expected
    for item in data:
        assert heapq.heappush_max(heap, item) is None
    for item in expected:
        assert heapq.heappop_max(heap) == item

heap = [7, 5, 3]
assert heapq.heapreplace_max(heap, 1) == 7
assert heapq.heappushpop_max(heap, 9) == 9
assert heapq.heappushpop_max(heap, 4) == 5
assert [heapq.heappop_max(heap), heapq.heappop_max(heap),
        heapq.heappop_max(heap)] == [4, 3, 1]
assert heapq.heapify_max([]) is None
item = object()
assert heapq.heappushpop_max([], item) is item
raises(IndexError, heapq.heappop_max, [])
raises(IndexError, heapq.heapreplace_max, [], item)

for function in (heapq.heapify_max, heapq.heappop_max):
    raises(TypeError, function)
    raises(TypeError, function, [], 1)
    raises(TypeError, function, heap=[])
for function in (heapq.heappush_max, heapq.heapreplace_max,
                 heapq.heappushpop_max):
    raises(TypeError, function)
    raises(TypeError, function, [])
    raises(TypeError, function, [], 1, 2)
    raises(TypeError, function, [], item=1)
    raises(TypeError, function, heap=[], item=1)
for value in (None, (), {}, "abc", 1):
    raises(TypeError, heapq.heapify_max, value)
    raises(TypeError, heapq.heappop_max, value)
    raises(TypeError, heapq.heappush_max, value, 1)
    raises(TypeError, heapq.heapreplace_max, value, 1)
    raises(TypeError, heapq.heappushpop_max, value, 1)


class LT:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return self.value < other.value

    def __eq__(self, other):
        raise AssertionError("heap operations must not use equality")

    def __gt__(self, other):
        raise AssertionError("heap operations must use lt")

    def __le__(self, other):
        raise AssertionError("heap operations must use lt")

    def __ge__(self, other):
        raise AssertionError("heap operations must use lt")


first = LT(3)
equal = LT(3)
low = LT(1)
heap = [first]
assert heapq.heappushpop_max(heap, equal) is equal
assert heap[0] is first
assert heapq.heappushpop_max(heap, low) is first
assert heap[0] is low
heapq.heappush_max(heap, equal)
assert heapq.heapreplace_max(heap, first) is equal
assert heapq.heappop_max(heap) is first
assert heapq.heappop_max(heap) is low

# Observe ordinary Python fallback hooks, including a single truth test for an
# empty heap. The native accelerator deliberately bypasses these hooks.
log = []


class Hooks(list):
    def append(self, item):
        log.append("append")
        return list.append(self, item)

    def pop(self):
        log.append("pop")
        return list.pop(self)

    def __len__(self):
        log.append("len")
        return list.__len__(self)

    def __bool__(self):
        log.append("bool")
        return list.__len__(self) != 0

    def __getitem__(self, index):
        log.append("get")
        return list.__getitem__(self, index)

    def __setitem__(self, index, value):
        log.append("set")
        list.__setitem__(self, index, value)


heap = Hooks()
log.clear()
assert heapq.heappushpop_max(heap, 1) == 1
assert log == ["bool"]
log.clear()
heapq.heappush_max(heap, 3)
assert log == ["append", "len", "get", "set"]
log.clear()
assert heapq.heappop_max(heap) == 3
assert log == ["pop", "bool"]


class BadLength(list):
    def __len__(self):
        return -1


raises(ValueError, heapq.heapify_max, BadLength())

comparison_error = ValueError("comparison failed")


class Explodes:
    def __lt__(self, other):
        raise comparison_error


left = Explodes()
right = Explodes()
heap = [left]
assert raises(ValueError, heapq.heappush_max, heap, right) is comparison_error
assert len(heap) == 2 and heap[0] is left and heap[1] is right
heap = [left]
assert raises(ValueError, heapq.heappushpop_max, heap, right) is comparison_error
assert len(heap) == 1 and heap[0] is left


class Clears:
    def __lt__(self, other):
        target.clear()
        return True


for operation in ("heapify_max", "heappush_max", "heappop_max",
                  "heapreplace_max", "heappushpop_max"):
    a = Clears()
    b = Clears()
    target = [a, b, a]
    function = getattr(heapq, operation)
    if operation == "heapify_max" or operation == "heappop_max":
        raises(IndexError, function, target)
    else:
        raises(IndexError, function, target, b)
    assert target == []
