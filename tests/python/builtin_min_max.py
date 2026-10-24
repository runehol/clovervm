# Self-checking cases compared with CPython 3.14.5. Only the VM's existing
# iterable and callable forms are exercised.


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


calls = []


def key(item):
    calls.append(item)
    return item[0]


rows = [(2, "a"), (1, "b"), (2, "c"), (1, "d")]
for case in ((min, rows[1]), (max, rows[0])):
    function = case[0]
    winner = case[1]
    calls.clear()
    assert function(rows, key=key) is winner
    assert calls == rows
    calls.clear()
    assert function(*rows, key=key) is winner
    assert calls == rows
    assert function(rows, key=key, default=None) is winner
    marker = []
    assert function([], default=marker) is marker
    assert function([], default=None, key=42) is None
    assert function([], default=marker, key=key) is marker
    assert function([marker], key=None) is marker
    assert function([3, 1, 2], key=None) == function(3, 1, 2)
    assert function("cba") == ("a" if function is min else "c")
    assert function(["3", "1", "2"], key=int) == ("1" if function is min else "3")
    assert function([-3, 1, -2], key=abs) == (1 if function is min else -3)
    raises(ValueError, function, [])
    raises(TypeError, function)
    raises(TypeError, function, [1], key=42)
    raises(TypeError, function, 1, 2, default=None)
    raises(TypeError, function, 1, 2, default=0, key=42)
    raises(TypeError, function, iterable=[])
    raises(TypeError, function, [], unknown=True)


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
        raise AssertionError("must remain streaming")


def streaming_key(item):
    calls.append("key")
    return -item


for function in (min, max):
    calls.clear()
    assert function(Source(), key=streaming_key) == (3 if function is min else 1)
    assert calls == ["iter", 0, "key", 1, "key", 2, "key", 3]
    calls.clear()
    raises(TypeError, function, Source(), unknown=True)
    assert calls == []
    raises(TypeError, function, Source(), 1, default=None)
    assert calls == []
    # Unknown keyword validation precedes the multiple-argument default error.
    error = raises(TypeError, function, Source(), 1, default=None, unknown=True)
    assert "unexpected keyword" in str(error)
    error = raises(TypeError, function, [], key=None, default=None, unknown=True)
    assert "at most 2 keyword arguments" in str(error)
    error = raises(TypeError, function, unknown=True)
    assert "at least 1 argument" in str(error)
    raises(TypeError, function, Source(), key=42)
    assert calls == ["iter", 0]


failure = RuntimeError("failed")


def bad_key(item):
    calls.append("key")
    raise failure


class BadIterator:
    def __iter__(self):
        return self

    def __next__(self):
        raise failure


def stop_key(item):
    raise StopIteration("key failure")


class BadOrder:
    def __lt__(self, other):
        calls.append("compare")
        raise failure

    def __gt__(self, other):
        calls.append("compare")
        raise failure


def order_key(item):
    calls.append("key")
    return BadOrder()


for function in (min, max):
    calls.clear()
    assert raises(RuntimeError, function, Source(), key=bad_key) is failure
    assert calls == ["iter", 0, "key"]
    calls.clear()
    assert raises(RuntimeError, function, Source(), key=order_key) is failure
    assert calls == ["iter", 0, "key", 1, "key", "compare"]
    assert raises(RuntimeError, function, BadIterator(), default=None) is failure
    raises(StopIteration, function, [1], key=stop_key)


# Comparison results use normal truth conversion, including its failures.
class Truth:
    def __bool__(self):
        calls.append("bool")
        return False


class Order:
    def __lt__(self, other):
        return Truth()

    def __gt__(self, other):
        return Truth()


first = Order()
second = Order()
for function in (min, max):
    calls.clear()
    assert function(first, second) is first
    assert calls == ["bool"]


class BadTruth:
    def __bool__(self):
        raise failure


class BadTruthOrder:
    def __lt__(self, other):
        return BadTruth()

    def __gt__(self, other):
        return BadTruth()


for function in (min, max):
    assert raises(RuntimeError, function, BadTruthOrder(), BadTruthOrder()) is failure


# A key is evaluated even for the first/only item, including key=None handling.
for function in (min, max):
    calls.clear()
    assert function([rows[0]], key=key) is rows[0]
    assert calls == [rows[0]]
    calls.clear()
    assert function([], key=bad_key, default=None) is None
    assert calls == []


# Unordered values retain the first item rather than being ranked specially.
nan = float("nan")
assert min(nan, 1) is nan
assert max(nan, 1) is nan
assert min(1, nan) == 1
assert max(1, nan) == 1
