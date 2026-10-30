# Python test set -- heapq module
#
# Portions adapted from CPython's Lib/test/test_heapq.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
#
# Failure-state expectations were probed against the Python fallback in
# CPython 3.14.5; the C accelerator can leave different partial mutations.

import heapq


def raises(error, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


def invariant(heap):
    for child in range(1, len(heap)):
        assert not heap[child] < heap[(child - 1) // 2]


def test_numbers():
    for values in ([], [1], [4, 4, 4], [5, 1, 5, 2, 1, 0, -3],
                   list(range(40)), list(range(39, -1, -1))):
        heap = values.copy()
        alias = heap
        assert heapq.heapify(heap) is None
        assert heap is alias
        invariant(heap)
        popped = []
        while heap:
            popped.append(heapq.heappop(heap))
            invariant(heap)
        assert len(popped) == len(values)
        for i in range(1, len(popped)):
            assert not popped[i] < popped[i - 1]
        # Check multiplicities as well as order, without depending on sort.
        for value in values:
            assert popped.count(value) == values.count(value)
        heap = []
        for value in values:
            assert heapq.heappush(heap, value) is None
            invariant(heap)
        for value in popped:
            assert heapq.heappop(heap) == value
        assert heap == []


def test_replacements():
    heap = [1, 3, 2]
    assert heapq.heapreplace(heap, 4) == 1
    assert len(heap) == 3
    invariant(heap)
    assert heapq.heapreplace(heap, -1) == 2
    assert heap[0] == -1
    invariant(heap)
    before = heap.copy()
    assert heapq.heappushpop(heap, -2) == -2
    assert heap == before
    assert heapq.heappushpop(heap, -1) == -1
    assert heap == before
    assert heapq.heappushpop(heap, 9) == -1
    assert len(heap) == 3
    invariant(heap)
    assert heapq.heappop(heap) == 3
    assert heapq.heappop(heap) == 4
    assert heapq.heappop(heap) == 9


def test_empty_and_identity():
    heap = []
    item = object()
    assert heapq.heapify(heap) is None
    assert heapq.heappushpop(heap, item) is item
    assert heap == []
    raises(IndexError, heapq.heappop, heap)
    raises(IndexError, heapq.heapreplace, heap, item)
    assert heap == []
    assert heapq.heappush(heap, item) is None
    assert heapq.heappop(heap) is item
    assert heap == []
    other = object()
    heap = [item]
    assert heapq.heapreplace(heap, other) is item
    assert heap[0] is other


class LT:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return self.value < other.value

    def __eq__(self, other):
        raise AssertionError("heapq must not use equality")

    def __le__(self, other):
        raise AssertionError("heapq must not use <=")

    def __gt__(self, other):
        raise AssertionError("heapq must not reverse the comparison")

    def __ge__(self, other):
        raise AssertionError("heapq must not use >=")


def test_lt_and_ties():
    first = LT(2)
    equal = LT(2)
    low = LT(1)
    heap = [first]
    assert heapq.heappushpop(heap, equal) is equal
    assert heap[0] is first
    assert heapq.heappushpop(heap, low) is low
    assert heap[0] is first
    assert heapq.heapreplace(heap, equal) is first
    assert heap[0] is equal
    heapq.heappush(heap, low)
    assert heapq.heappop(heap) is low
    assert heapq.heappop(heap) is equal
    # Equal children choose the right child; no equality/identity shortcut.
    root = LT(3)
    heap = [root, first, equal]
    heapq.heapify(heap)
    assert heap[0] is equal
    invariant(heap)
    assert heapq.heappop(heap) is equal
    assert heapq.heappop(heap) is first
    assert heapq.heappop(heap) is root
    heap = [first, first, first]
    heapq.heapify(heap)
    assert heapq.heappushpop(heap, first) is first
    while heap:
        assert heapq.heappop(heap) is first


def test_call_contract():
    for function in (heapq.heapify, heapq.heappop):
        heap = [1]
        raises(TypeError, function)
        raises(TypeError, function, heap, 2)
        raises(TypeError, function, heap=heap)
        assert heap == [1]
    for function in (heapq.heappush, heapq.heapreplace, heapq.heappushpop):
        heap = [1]
        raises(TypeError, function)
        raises(TypeError, function, heap)
        raises(TypeError, function, heap, 2, 3)
        raises(TypeError, function, heap, item=2)
        raises(TypeError, function, heap=heap, item=2)
        assert heap == [1]
    assert heapq.__all__ == ["heapify", "heappush", "heappop",
                             "heapreplace", "heappushpop", "heapify_max",
                             "heappush_max", "heappop_max", "heapreplace_max",
                             "heappushpop_max", "nlargest", "nsmallest", "merge"]
    assert hasattr(heapq, "merge")


class Duck:
    def __len__(self):
        raise AssertionError("input check must precede callbacks")

    def __bool__(self):
        raise AssertionError("input check must precede callbacks")

    def __getitem__(self, index):
        raise AssertionError("input check must precede callbacks")

    def append(self, item):
        raise AssertionError("input check must precede callbacks")

    def pop(self):
        raise AssertionError("input check must precede callbacks")


def test_input_types():
    for value in (None, 1, "", (), (1,), {}, Duck()):
        raises(TypeError, heapq.heapify, value)
        raises(TypeError, heapq.heappop, value)
        raises(TypeError, heapq.heappush, value, 2)
        raises(TypeError, heapq.heapreplace, value, 2)
        raises(TypeError, heapq.heappushpop, value, 2)
    class Plain(list):
        pass
    heap = Plain([4, 2, 3])
    alias = heap
    heapq.heapify(heap)
    heapq.heappush(heap, 1)
    assert heapq.heappop(heap) == 1
    assert heapq.heapreplace(heap, 6) == 2
    assert heapq.heappushpop(heap, 5) == 3
    assert heap is alias
    assert heap.__class__ is Plain
    invariant(heap)


events = []


class HookList(list):
    def __len__(self):
        events.append("len")
        return list.__len__(self)

    def __getitem__(self, index):
        events.append("get")
        return list.__getitem__(self, index)

    def __setitem__(self, index, value):
        events.append("set")
        list.__setitem__(self, index, value)

    def append(self, item):
        events.append("append")
        list.append(self, item)

    def pop(self):
        events.append("pop")
        return list.pop(self)


def test_subclass_hooks():
    for mode in range(5):
        heap = HookList([1, 3, 2])
        events.clear()
        if mode == 0:
            heapq.heapify(heap)
        elif mode == 1:
            heapq.heappush(heap, 4)
        elif mode == 2:
            assert heapq.heappop(heap) == 1
        elif mode == 3:
            assert heapq.heapreplace(heap, 4) == 1
        else:
            assert heapq.heappushpop(heap, 4) == 1
        assert "len" in events
        assert "get" in events
        assert "set" in events
        if mode == 1:
            assert events[0] == "append"
        if mode == 2:
            assert events[0] == "pop"
        invariant(heap)


def test_pushpop_stateful_truth():
    class StatefulHeap(list):
        def __init__(self):
            list.__init__(self, [10])
            self.calls = 0

        def __bool__(self):
            self.calls += 1
            return self.calls > 1

    heap = StatefulHeap()
    assert heapq.heappushpop(heap, 1) == 1
    assert heap.calls == 1
    assert list.__len__(heap) == 1
    assert heap[0] == 10


class LengthInt(int):
    def forbidden(self, *args):
        raise AssertionError("length integer subclass hook was invoked")

    __int__ = forbidden
    __index__ = forbidden
    __floordiv__ = forbidden
    __sub__ = forbidden
    __lt__ = forbidden
    __gt__ = forbidden


class TypedLengthHeap(list):
    def __len__(self):
        return LengthInt(list.__len__(self))


def test_integer_subclass_lengths():
    heap = TypedLengthHeap([3, 1, 2])
    assert heapq.heapify(heap) is None
    assert heap[0] == 1
    assert heapq.heappush(heap, 0) is None
    assert heap[0] == 0
    assert heapq.heapreplace(heap, 4) == 0
    assert heapq.heappushpop(heap, 5) == 1
    for value in (2, 3, 4, 5):
        assert heapq.heappop(heap) == value
    assert list.__len__(heap) == 0


class InvalidLengthHeap(list):
    def __len__(self):
        return self.length_result

    def __bool__(self):
        # Reach the size reads in _siftup as well as those in heapify/heappush,
        # without the VM's separate truth-length validation preempting them.
        return True


def test_invalid_lengths():
    cases = (
        ("heapify", [1, 3, 2]),
        ("heappush", [1, 3, 2, 4]),
        ("heappop", [2, 3]),
        ("heapreplace", [4, 3, 2]),
        ("heappushpop", [4, 3, 2]),
    )
    for result in (-1, -(2**100), LengthInt(-1), 1.5, "3", 2**63):
        if isinstance(result, int):
            plain = int.__index__(result)
            if plain < 0:
                error = ValueError
            else:
                error = OverflowError
        else:
            error = TypeError
        for case in cases:
            heap = InvalidLengthHeap([1, 3, 2])
            heap.length_result = result
            function = getattr(heapq, case[0])
            if case[0] == "heapify" or case[0] == "heappop":
                raises(error, function, heap)
            else:
                raises(error, function, heap, 4)
            # Match the fallback's partial mutation before the length error.
            expected = case[1]
            assert list.__len__(heap) == len(expected)
            for i in range(len(expected)):
                assert heap[i] == expected[i]


comparison_error = ValueError("comparison failed")
trace = []
fail_at = 0


class Traced(LT):
    def __lt__(self, other):
        trace.append((self.value, other.value))
        if len(trace) == fail_at:
            raise comparison_error
        return self.value < other.value


def test_comparison_direction():
    global fail_at
    fail_at = 0
    heap = [Traced(1), Traced(3), Traced(2)]
    trace.clear()
    heapq.heappush(heap, Traced(0))
    assert trace == [(0, 3), (0, 1)]
    trace.clear()
    heapq.heappop(heap)
    assert trace == [(1, 2), (3, 1)]
    trace.clear()
    heapq.heappushpop(heap, Traced(4))
    assert trace == [(1, 4), (3, 2), (4, 2)]
    same = Traced(2)
    heap = [same]
    trace.clear()
    assert heapq.heappushpop(heap, same) is same
    assert trace == [(2, 2)]


def test_failure_states():
    global fail_at
    # First and later comparison failures pin down Python assignment order,
    # including duplicated references after a value has moved down a path.
    cases = (
        ("heapify", [9, 7, 5, 3, 1], 0, 1, [9, 7, 5, 3, 1]),
        ("heapify", [9, 7, 5, 3, 1], 0, 2, [9, 1, 5, 3, 7]),
        ("heappush", [1, 3, 2, 7, 8, 4, 5], 0, 1, [1, 3, 2, 7, 8, 4, 5, 0]),
        ("heappush", [1, 3, 2, 7, 8, 4, 5], 0, 2, [1, 3, 2, 7, 8, 4, 5, 7]),
        ("heappop", [1, 3, 2, 7, 8, 4, 5], 0, 1, [5, 3, 2, 7, 8, 4]),
        ("heappop", [1, 3, 2, 7, 8, 4, 5], 0, 2, [2, 3, 4, 7, 8, 5]),
        ("heapreplace", [1, 3, 2, 7, 8, 4, 5], 6, 1, [6, 3, 2, 7, 8, 4, 5]),
        ("heapreplace", [1, 3, 2, 7, 8, 4, 5], 6, 2, [2, 3, 2, 7, 8, 4, 5]),
        ("heappushpop", [1, 3, 2, 7, 8, 4, 5], 6, 1, [1, 3, 2, 7, 8, 4, 5]),
        ("heappushpop", [1, 3, 2, 7, 8, 4, 5], 6, 2, [6, 3, 2, 7, 8, 4, 5]),
        ("heappushpop", [1, 3, 2, 7, 8, 4, 5], 6, 3, [2, 3, 2, 7, 8, 4, 5]),
    )
    for case in cases:
        heap = []
        for value in case[1]:
            heap.append(Traced(value))
        item = Traced(case[2])
        originals = heap.copy()
        originals.append(item)
        trace.clear()
        fail_at = case[3]
        function = getattr(heapq, case[0])
        if case[0] == "heapify" or case[0] == "heappop":
            assert raises(ValueError, function, heap) is comparison_error
        else:
            assert raises(ValueError, function, heap, item) is comparison_error
        expected = case[4]
        assert len(heap) == len(expected)
        assert len(trace) == fail_at
        for i in range(len(heap)):
            assert heap[i].value == expected[i]
            # Every stored reference is the original object with that value.
            for original in originals:
                if original.value == expected[i]:
                    assert heap[i] is original
    fail_at = 0


class Truth:
    def __init__(self, value):
        self.value = value

    def __bool__(self):
        events.append("bool")
        return self.value


class TruthLT(LT):
    def __lt__(self, other):
        return Truth(self.value < other.value)


class BadTruth:
    def __bool__(self):
        raise comparison_error


class BadTruthLT(LT):
    def __lt__(self, other):
        return BadTruth()


def test_comparison_truth_and_errors():
    heap = [TruthLT(2)]
    low = TruthLT(1)
    events.clear()
    heapq.heappush(heap, low)
    assert events == ["bool"]
    assert heap[0] is low
    item = BadTruthLT(0)
    before = heap.copy()
    assert raises(ValueError, heapq.heappush, heap, item) is comparison_error
    assert len(heap) == 3
    assert heap[0] is before[0]
    assert heap[1] is before[1]
    assert heap[2] is item
    # Unorderable elements are accepted until an actual comparison is needed.
    heap = [object()]
    raises(TypeError, heapq.heappush, heap, object())
    assert len(heap) == 2
    class LEOnly:
        def __le__(self, other):
            raise AssertionError("must use <")
    heap = [LEOnly()]
    raises(TypeError, heapq.heappush, heap, LEOnly())


class Clearing(LT):
    def __init__(self, value, heap):
        self.value = value
        self.heap = heap

    def __lt__(self, other):
        self.heap.clear()
        return self.value < other.value


def test_resizing_callbacks():
    for mode in range(5):
        heap = []
        for value in (1, 3, 2):
            heap.append(Clearing(value, heap))
        item = Clearing(4, heap)
        if mode == 0:
            raises(IndexError, heapq.heapify, heap)
        elif mode == 1:
            raises(IndexError, heapq.heappush, heap, item)
        elif mode == 2:
            raises(IndexError, heapq.heappop, heap)
        elif mode == 3:
            raises(IndexError, heapq.heapreplace, heap, item)
        else:
            raises(IndexError, heapq.heappushpop, heap, item)
        assert heap == []
    # The fallback does not reject resizing unconditionally: a false root
    # comparison returns the incoming item without touching the heap again.
    heap = []
    heap.append(Clearing(1, heap))
    item = Clearing(0, heap)
    assert heapq.heappushpop(heap, item) is item
    assert heap == []


test_numbers()
test_replacements()
test_empty_and_identity()
test_lt_and_ties()
test_call_contract()
test_input_types()
test_subclass_hooks()
test_pushpop_stateful_truth()
test_integer_subclass_lengths()
test_invalid_lengths()
test_comparison_direction()
test_failure_states()
test_comparison_truth_and_errors()
test_resizing_callbacks()
