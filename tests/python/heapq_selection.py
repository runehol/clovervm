# Python test set -- heapq bounded selection
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


def key(row):
    return row[0]


# Group the same assertions by behavior to stay within the compiler's current
# 256 global-read sites per code object. Minimal independent reproducer:
# subprocess.run([vm, "-c", "value = 1\n" + "value\n" * 257])
# raises SystemError: module global read cache table index out of range.
def test_selection_values_and_signatures():
    for size in range(30):
        data = []
        for i in range(size):
            data.append(((i * 19 + size) % 13, i))
        for n in (0, 1, 2, 5, 15, 29, 30, 35):
            for function in (heapq.nsmallest, heapq.nlargest):
                reverse = function is heapq.nlargest
                expected = sorted(data, reverse=reverse)[:n]
                assert function(n, data) == expected
                assert function(n, iter(data)) == expected
                expected = sorted(data, key=key, reverse=reverse)[:n]
                assert function(n, data, key) == expected
                assert function(n, iter(data), key=key) == expected

    for function in (heapq.nsmallest, heapq.nlargest):
        assert function(n=2, iterable=[3, 1, 2], key=None) == function(2, [3, 1, 2])
        assert function(1.0, [3, 1, 2]) == function(1, [3, 1, 2])
        assert function(True, [3, 1, 2]) == function(1, [3, 1, 2])
        assert function(False, [3, 1, 2]) == []
        assert function(-3, [3, 1, 2]) == []
        assert function(1, []) == []
        assert function(2, (), key=42) == []
        assert function(0, [1], key=42) == []
        raises(TypeError, function)
        raises(TypeError, function, 1)
        raises(TypeError, function, 1, [], None, None)
        raises(TypeError, function, 1, [], n=1)
        raises(TypeError, function, 1, [], unknown=True)
        raises(TypeError, function, 2.0, iter([1, 2, 3]))
        raises(TypeError, function, 1, [1], key=42)
        raises(TypeError, function, 0, object())

test_selection_values_and_signatures()

log = []


class Stream:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def __len__(self):
        log.append("len")
        raise TypeError("unsized")

    def __iter__(self):
        log.append("iter")
        return self

    def __next__(self):
        log.append("next")
        if self.pos == len(self.data):
            raise StopIteration
        value = self.data[self.pos]
        self.pos += 1
        return value


keys = []


def traced_key(value):
    keys.append(value)
    return value


for function in (heapq.nsmallest, heapq.nlargest):
    for n in (-2, 0):
        log.clear()
        keys.clear()
        stream = Stream([3, 1, 2])
        assert function(n, stream, traced_key) == []
        assert log == ["len", "iter", "iter"]
        assert keys == [] and stream.pos == 0
    for n in (1, 2, 5):
        log.clear()
        keys.clear()
        stream = Stream([3, 1, 2])
        function(n, stream, traced_key)
        assert keys == [3, 1, 2]
        assert stream.pos == 3
        if n == 1:
            assert log[0] == "iter" and "len" not in log
        else:
            assert log[0] == "len"
    # Key execution interleaves with consumption, before the next item.
    assert function(2, [3, 1, 2], traced_key) == function(2, [3, 1, 2])


class Payload:
    def __lt__(self, other):
        raise AssertionError("equal keys must not compare payloads")


rows = [Payload(), Payload(), Payload(), Payload()]


def same_key(value):
    return 0


for function in (heapq.nsmallest, heapq.nlargest):
    for n in (1, 2, 4, 6):
        result = function(n, rows, same_key)
        assert len(result) == min(n, len(rows))
        for i in range(len(result)):
            assert result[i] is rows[i]

failure = ValueError("callback failure")


def bad_key(value):
    raise failure


class BadIter:
    def __iter__(self):
        raise failure


class BadNext:
    def __iter__(self):
        return self

    def __next__(self):
        raise failure


class BadLen:
    def __len__(self):
        raise failure

    def __iter__(self):
        return iter([3, 1, 2])


for function in (heapq.nsmallest, heapq.nlargest):
    for n in (0, 1, 2, 5):
        assert raises(ValueError, function, n, BadIter()) is failure
        if n != 0:
            assert raises(ValueError, function, n, BadNext()) is failure
            assert raises(ValueError, function, n, [3, 1, 2], bad_key) is failure
        if n == 1:
            assert function(n, BadLen()) == function(n, [3, 1, 2])
        else:
            assert raises(ValueError, function, n, BadLen()) is failure

# Key callbacks can extend the source and recursively invoke selection. The
# source list iterator remains live; keys are neither repeated nor precomputed.
source = []
seen = []


def mutate_key(value):
    seen.append(value)
    if value == 3:
        source.append(0)
        assert heapq.nlargest(2, [1, 4, 2]) == [4, 2]
    return value


for function in (heapq.nsmallest, heapq.nlargest):
    source = [3, 1, 2]
    seen.clear()
    expected = function(2, [3, 1, 2, 0])
    assert function(2, iter(source), mutate_key) == expected
    assert seen == [3, 1, 2, 0]

# Tuple decorations retain equality-before-ordering behavior. Direct heap
# comparisons use lt, and decorated values still reject le-only ordering.
class Explodes:
    def __eq__(self, other):
        raise failure

    def __lt__(self, other):
        raise failure


for function in (heapq.nsmallest, heapq.nlargest):
    for n in (1, 2, 4):
        assert raises(ValueError, function, n,
                      [Explodes(), Explodes(), Explodes()]) is failure

# Initial heap comparisons happen after n keys, before reading the remainder.
# This distinguishes bounded selection from materializing/sorting the input.
comparisons = []


class Key:
    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        return self.value == other.value

    def __lt__(self, other):
        comparisons.append(len(keys))
        return self.value < other.value


def counting_key(value):
    keys.append(value)
    return Key(value)


class Interleaved:
    def __init__(self):
        self.pos = 0

    def __iter__(self):
        return self

    def __next__(self):
        assert len(keys) == self.pos
        if self.pos == 20:
            raise StopIteration
        value = (self.pos * 7) % 23
        self.pos += 1
        return value


for function in (heapq.nsmallest, heapq.nlargest):
    keys.clear()
    comparisons.clear()
    result = function(3, Interleaved(), counting_key)
    assert len(keys) == 20
    assert comparisons[0] == 3
    assert result == function(3, keys)

# The sized sorting shortcut snapshots the input before key callbacks; callback
# appends therefore differ intentionally from the live bounded-iterator path.
for function in (heapq.nsmallest, heapq.nlargest):
    source = [3, 1, 2]
    seen.clear()
    expected = function(5, [3, 1, 2])
    assert function(5, source, mutate_key) == expected
    assert seen == [3, 1, 2]
    assert source == [3, 1, 2, 0]


def test_list_subclass_and_length_errors():
    class Rows(list):
        def __len__(self):
            log.append("len")
            return list.__len__(self)

        def __iter__(self):
            log.append("iter")
            return list.__iter__(self)

    class ReportedEmpty(list):
        def __len__(self):
            return 0

    class NegativeLength(list):
        def __len__(self):
            return -1

    class OversizedLength(list):
        def __len__(self):
            return 2**63

    for function in (heapq.nsmallest, heapq.nlargest):
        for n in (0, 1, 2, 5):
            log.clear()
            rows = Rows([3, 1, 2])
            assert function(n, rows) == function(n, [3, 1, 2])
            if n == 1:
                assert log == ["iter"]
            else:
                assert log[0] == "len"
                assert "iter" in log
        keys.clear()
        # CPython consults the reported size before the bounded zero-n path.
        assert function(0, ReportedEmpty([3, 1, 2]), traced_key) == []
        assert keys == [3, 1, 2]
        raises(ValueError, function, 0, NegativeLength([3, 1, 2]))
        raises(OverflowError, function, 2, OversizedLength([3, 1, 2]))
        assert function(1, NegativeLength([3, 1, 2])) == function(1, [3, 1, 2])


test_list_subclass_and_length_errors()


def late_failure_key(value):
    keys.append(value)
    if len(keys) == 3:
        raise failure
    return value


def test_late_callback_failure_consumption():
    for function in (heapq.nsmallest, heapq.nlargest):
        for n in (1, 2, 5):
            keys.clear()
            stream = Stream([3, 1, 2, 4])
            assert raises(ValueError, function, n, stream,
                          late_failure_key) is failure
            assert keys == [3, 1, 2]
            assert stream.pos == 3
            assert next(stream) == 4


test_late_callback_failure_consumption()
