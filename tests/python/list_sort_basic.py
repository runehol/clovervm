# Self-checking behavior shared with CPython 3.14.5. Comparison traces and the
# partial order left after comparison failure are intentionally not specified.


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


for case in (
    ([], []), ([1], [1]), ([3, 1, 2], [1, 2, 3]),
    ([3, 1, 3, 0, -1], [-1, 0, 1, 3, 3]),
    ([2.5, -1, 2, False], [-1, False, 2, 2.5]),
    ([2**80, -2**80, 3], [-2**80, 3, 2**80]),
    (["b", "a", "aa"], ["a", "aa", "b"]),
    ([(2, 0), (1, 9), (1, 2)], [(1, 2), (1, 9), (2, 0)]),
    ([[2], [1, 9], [1]], [[1], [1, 9], [2]]),
):
    values = case[0]
    expected = case[1]
    alias = values
    assert values.sort() is None
    assert values is alias
    assert values == expected
    assert values.sort(reverse=True) is None
    expected.reverse()
    assert values == expected


# Exercise several merge widths, duplicate keys, and permutation cycles.
for length in range(70):
    values = []
    expected = []
    for i in range(length):
        values.append((length - i - 1, i))
        expected.append((i, length - i - 1))
    values.sort()
    assert values == expected


calls = []


def key(row):
    calls.append(row)
    return row[0]


original = [(2, "a"), (1, "b"), (2, "c"), (1, "d"), (2, "e")]
for reverse in (False, True):
    calls.clear()
    rows = original.copy()
    assert list.sort(rows, key=key, reverse=reverse) is None
    assert calls == original
    if reverse:
        assert rows == [(2, "a"), (2, "c"), (2, "e"), (1, "b"), (1, "d")]
    else:
        assert rows == [(1, "b"), (1, "d"), (2, "a"), (2, "c"), (2, "e")]

calls.clear()
assert [].sort(key=key) is None
assert calls == []
assert [(1, "x")].sort(key=key) is None
assert calls == [(1, "x")]

values = [-3, 1, -2]
values.sort(key=abs)
assert values == [1, -2, -3]
values = ["3", "1", "2"]
values.sort(key=int)
assert values == ["1", "2", "3"]

assert [].sort(key=42) is None
raises(TypeError, list.sort, [1], key=42)
raises(TypeError, list.sort)
raises(TypeError, list.sort, ())
raises(TypeError, list.sort, (), key=None)
raises(TypeError, list.sort, [2, 1], None)
raises(TypeError, list.sort, [2, 1], None, True)
raises(TypeError, list.sort, [2, 1], unexpected=True)
raises(TypeError, list.sort, self=[])


class Reverse:
    def __bool__(self):
        calls.append("reverse")
        return True


calls.clear()
values = [(1, "a"), (2, "b")]
values.sort(key=key, reverse=Reverse())
assert calls == ["reverse", (1, "a"), (2, "b")]
assert values == [(2, "b"), (1, "a")]


class BadReverse:
    def __bool__(self):
        raise RuntimeError("reverse failed")


calls.clear()
values = original.copy()
raises(RuntimeError, list.sort, values, key=key, reverse=BadReverse())
assert values == original
assert calls == []
for reverse in (None, [], "", 0.0):
    values = [2, 1]
    values.sort(reverse=reverse)
    assert values == [1, 2]


# Equal keys remain in source order across multiple merge widths.
def row_key(row):
    return row[0]


for length in (7, 31, 32, 33, 127, 129):
    original = []
    for i in range(length):
        original.append((i % 7, i))
    for reverse in (False, True):
        values = original.copy()
        values.sort(key=row_key, reverse=reverse)
        expected = []
        groups = [6, 5, 4, 3, 2, 1, 0] if reverse else [0, 1, 2, 3, 4, 5, 6]
        for group in groups:
            for row in original:
                if row[0] == group:
                    expected.append(row)
        assert values == expected
