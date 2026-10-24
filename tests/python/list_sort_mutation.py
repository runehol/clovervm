# Callback visibility, reentry, mutation, and failure contracts, checked against
# CPython 3.14.5. Do not assert a particular comparison trace or partial order.


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


source = []
calls = []
operation = "none"


def mutate():
    if operation == "append":
        source.append(99)
    if operation == "append_pop":
        source.append(99)
        source.pop()
    if operation == "append_clear":
        source.append(99)
        source.clear()
    if operation == "insert_delete":
        source.insert(0, 99)
        del source[0]
    if operation == "extend_clear":
        source.extend((98, 99))
        source.clear()
    if operation == "append_set_pop":
        source.append(99)
        source[0] = 98
        source.pop()
    if operation == "append_pop_nested":
        source.append(99)
        source.pop()
        source.sort()
    if operation == "nested_nonempty":
        source.extend([99, 98])
        source.sort()
        source.clear()
    if operation == "empty_operations":
        source.clear()
        source.reverse()
        source.extend([])
        list.__init__(source, [])
    if operation == "nested_empty":
        source.sort(key=42)
        source.sort(reverse=True)
    if operation == "failed_write":
        raises(IndexError, list.pop, source)
        raises(IndexError, list.__setitem__, source, 0, 99)
        raises(IndexError, list.__delitem__, source, 0)


def key(value):
    if len(calls) == 0:
        assert source == []
    calls.append(value)
    mutate()
    return value


for operation in ("none", "empty_operations", "nested_empty", "failed_write"):
    source = [3, 1, 2]
    calls.clear()
    assert source.sort(key=key) is None
    assert calls == [3, 1, 2]
    assert source == [1, 2, 3]

for operation in (
    "append", "append_pop", "append_clear", "insert_delete", "extend_clear",
    "append_set_pop", "append_pop_nested", "nested_nonempty",
):
    source = [3, 1, 2]
    calls.clear()
    error = raises(ValueError, list.sort, source, key=key)
    assert str(error) == "list modified during sort"
    assert source == [1, 2, 3]
    assert calls == [3, 1, 2]
    # The marker from a completed/failed sort must not poison the next call.
    source.sort()
    assert source == [1, 2, 3]


class Ordered:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        if len(calls) == 0:
            assert source == []
        calls.append("compare")
        mutate()
        return self.value < other.value


for operation in ("none", "nested_empty", "append_pop", "append_clear"):
    source = [Ordered(3), Ordered(1), Ordered(2)]
    calls.clear()
    if operation == "none" or operation == "nested_empty":
        assert source.sort() is None
    else:
        raises(ValueError, list.sort, source)
    assert len(calls) > 0
    assert source[0].value == 1
    assert source[1].value == 2
    assert source[2].value == 3


failure = RuntimeError("key failed")


def bad_key(value):
    assert source == []
    calls.append(value)
    if value == 1:
        source.append(99)
        source.pop()
        raise failure
    return value


source = [3, 1, 2]
calls.clear()
assert raises(RuntimeError, list.sort, source, key=bad_key) is failure
assert calls == [3, 1]
assert source == [3, 1, 2]
source.sort()
assert source == [1, 2, 3]


class BadCompare:
    def __lt__(self, other):
        assert source == []
        source.append(99)
        source.clear()
        raise failure


first = BadCompare()
second = BadCompare()
third = BadCompare()
source = [first, second, third]
assert raises(RuntimeError, list.sort, source) is failure
assert len(source) == 3
assert source.count(first) == 1
assert source.count(second) == 1
assert source.count(third) == 1


# A truth callback can mutate and then raise; its exception wins over the
# mutation error, and values remain a valid permutation of the originals.
class BadTruth:
    def __bool__(self):
        assert source == []
        source.append(99)
        source.pop()
        raise failure


class TruthCompare:
    def __lt__(self, other):
        return BadTruth()


first = TruthCompare()
second = TruthCompare()
source = [first, second]
assert raises(RuntimeError, list.sort, source) is failure
assert len(source) == 2
assert source.count(first) == 1
assert source.count(second) == 1


# A reverse callback runs before storage detaches and participates in the input.
class Reverse:
    def __bool__(self):
        assert source == [2, 1]
        source.append(3)
        return True


source = [2, 1]
source.sort(reverse=Reverse())
assert source == [3, 2, 1]


# Empty lists still truth-test reverse, but never call an invalid key.
source = []
assert source.sort(key=42) is None
source = [1]
raises(TypeError, list.sort, source, key=42)
assert source == [1]


class MutatingTruth:
    def __init__(self, result):
        self.result = result

    def __bool__(self):
        assert source == []
        source.append(99)
        source.pop()
        return self.result


class TruthOrdered:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return MutatingTruth(self.value < other.value)


source = [TruthOrdered(2), TruthOrdered(1)]
raises(ValueError, list.sort, source)
assert source[0].value == 1
assert source[1].value == 2


# Fail after different numbers of successful comparisons, including completed
# merge segments/passes. Only preservation of the original objects is required.
comparison_count = 0
fail_at = 0


class LaterFailure:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        global comparison_count
        assert source == []
        comparison_count += 1
        if comparison_count == fail_at:
            raise failure
        return self.value < other.value


for fail_at in range(1, 12):
    for reverse in (False, True):
        comparison_count = 0
        original = []
        for i in range(17):
            original.append(LaterFailure(16 - i))
        source = original.copy()
        assert raises(RuntimeError, list.sort, source, reverse=reverse) is failure
        assert len(source) == len(original)
        for item in original:
            assert source.count(item) == 1


# Shared tuple rows exercise the exact boundary needed by JSON's items() lists.
class ObservedKey(str):
    def __lt__(self, other):
        assert source == []
        return str.__lt__(self, other)


source = [(ObservedKey("z"), 1), (ObservedKey("a"), 2)]
alias = source
source.sort()
assert source is alias
assert source == [("a", 2), ("z", 1)]


class MutatingRowKey(str):
    def __lt__(self, other):
        source.append(("new", 3))
        source.pop()
        return str.__lt__(self, other)


source = [(MutatingRowKey("z"), 1), (MutatingRowKey("a"), 2)]
raises(ValueError, list.sort, source)
assert source == [("a", 2), ("z", 1)]


class RaisingRowKey(str):
    def __lt__(self, other):
        source.append(("new", 3))
        raise failure


source = [(RaisingRowKey("z"), 1), (RaisingRowKey("a"), 2)]
assert raises(RuntimeError, list.sort, source) is failure
assert len(source) == 2
assert source[0][0] == "z"
assert source[1][0] == "a"
source = [("z", 1), ("a", 2)]
source.sort()
assert source == [("a", 2), ("z", 1)]
