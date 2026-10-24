# Self-checking cases compared with CPython 3.14.5. The existing iteration
# contract omits length hints and sequence-only __getitem__ fallback.


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


for source in ([], [3, 1, 2], (3, 1, 2), "cba"):
    before = list(source)
    result = sorted(source)
    assert isinstance(result, list)
    assert result is not source
    assert list(source) == before
    expected = list(source)
    list.sort(expected)
    assert result == expected
    assert sorted(source, reverse=True) == sorted(expected, reverse=True)


# The current VM represents range as a single-use iterator. Sort it once,
# like any other iterator; repeatable range-container behavior is outside scope.
assert sorted(range(3)) == [0, 1, 2]


calls = []
original = [(2, "a"), (1, "b"), (2, "c"), (1, "d")]


def key(row):
    calls.append(row)
    return row[0]


class Reverse:
    def __bool__(self):
        calls.append("reverse")
        return True


result = sorted(original, key=key)
assert result == [(1, "b"), (1, "d"), (2, "a"), (2, "c")]
assert result[0] is original[1]
assert result[2] is original[0]
assert calls == original
calls.clear()
assert sorted(original, key=key, reverse=Reverse()) == [
    (2, "a"), (2, "c"), (1, "b"), (1, "d")
]
assert calls == ["reverse", (2, "a"), (1, "b"), (2, "c"), (1, "d")]
assert original == [(2, "a"), (1, "b"), (2, "c"), (1, "d")]
assert sorted([], key=42) == []
raises(TypeError, sorted, [1], key=42)
assert sorted([-3, 1, -2], key=abs) == [1, -2, -3]
assert sorted(["3", "1", "2"], key=int) == ["1", "2", "3"]


class Source:
    def __init__(self):
        self.index = 0

    def __iter__(self):
        calls.append("iter")
        return self

    def __next__(self):
        calls.append(self.index)
        if self.index == 3:
            raise StopIteration
        self.index += 1
        return self.index

    def __len__(self):
        return 3


# CPython also finishes consumption before keyword validation/reverse/key calls;
# its extra length-hint callback is outside CloverVM's current contract.
calls.clear()
source = Source()
raises(TypeError, sorted, source, unexpected=True)
assert calls == ["iter", 0, 1, 2, 3]
calls.clear()
raises(TypeError, sorted, source, iterable=[])
assert calls == ["iter", 3]
calls.clear()
raises(TypeError, sorted, source, source, unexpected=True)
assert calls == []
raises(TypeError, sorted)
raises(TypeError, sorted, iterable=[])
raises(TypeError, sorted, [], None)

failure = RuntimeError("callback failed")


def bad_key(item):
    calls.append(item)
    raise failure


calls.clear()
assert raises(RuntimeError, sorted, [3, 1], key=bad_key) is failure
assert calls == [3]
assert sorted([], key=bad_key) == []


class BadReverse:
    def __bool__(self):
        calls.append("reverse")
        raise failure


calls.clear()
assert raises(RuntimeError, sorted, Source(), key=bad_key, reverse=BadReverse()) is failure
assert calls == ["iter", 0, 1, 2, 3, "reverse"]


class BadIterator:
    def __iter__(self):
        return self

    def __next__(self):
        raise failure


calls.clear()
assert raises(RuntimeError, sorted, BadIterator(), key=bad_key, reverse=BadReverse()) is failure
assert calls == []


class BadOrder:
    def __lt__(self, other):
        raise failure


left = BadOrder()
right = BadOrder()
source = [left, right]
assert raises(RuntimeError, sorted, source) is failure
assert source[0] is left and source[1] is right


class ListSubclass(list):
    def __iter__(self):
        return iter([8, 5])

    def sort(self, *args, **kwargs):
        raise AssertionError("source sort must not be called")


source = ListSubclass([3, 1])
assert sorted(source) == [5, 8]
assert list.__getitem__(source, 0) == 3
assert list.__getitem__(source, 1) == 1
assert sorted(source).__class__ is list

# Key callbacks see the source intact, and deliberate source mutations cannot
# affect the private copy that sorted has already finished constructing.
source = [3, 1, 2]


def mutate_source(item):
    assert source != []
    source.append(9)
    return item


assert sorted(source, key=mutate_source) == [1, 2, 3]
assert source == [3, 1, 2, 9, 9, 9]
