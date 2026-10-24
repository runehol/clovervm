# Python test set -- itertools module
#
# Portions adapted from CPython's Lib/test/test_itertools.py (3.14 branch),
# with behavior cross-checked against CPython 3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

from itertools import chain, repeat, islice, starmap


def raises(error, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error:
        return
    raise AssertionError("expected exception")


class Traced:
    def __init__(self, values, events, name):
        self.values = iter(values)
        self.events = events
        self.name = name

    def __iter__(self):
        self.events.append((self.name, "iter"))
        return self

    def __next__(self):
        self.events.append((self.name, "next"))
        return next(self.values)

def capture(*args):
    return args


def add(a, b):
    return a + b

error = ValueError("input failed")


class Recovering:
    def __init__(self):
        self.position = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.position += 1
        if self.position == 1:
            raise error
        if self.position == 2:
            return (7,)
        raise StopIteration

class BadIterable:
    def __iter__(self):
        raise error


class ExhaustingOuter:
    def __init__(self):
        self.calls = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.calls += 1
        if self.calls == 1:
            raise StopIteration("outer payload", 42)
        return [9]


class ExhaustingInner:
    def __iter__(self):
        raise StopIteration("inner payload", 7)


calls = []


def record(*args):
    calls.append(args)
    return args

def fail(value):
    raise error

def stop(value):
    raise StopIteration

class Integer(int):
    pass


def test_chain():
    assert list(chain()) == []
    assert list(chain([], (), [])) == []
    assert list(chain([1, 2], (), [3], [4, 5])) == [1, 2, 3, 4, 5]
    assert list(chain.from_iterable([])) == []
    assert list(chain.from_iterable([[], [1, 2], (), [3]])) == [1, 2, 3]
    # String iteration is not yet supported by the VM's iterator protocol.
    assert list(chain(("a", "b"), ("c", "d"))) == ["a", "b", "c", "d"]

    events = []
    first = Traced([1], events, "first")
    second = Traced([2], events, "second")
    joined = chain(first, second)
    assert iter(joined) is joined
    assert events == []
    assert next(joined) == 1
    assert events == [("first", "iter"), ("first", "next")]
    assert next(joined) == 2
    assert events == [("first", "iter"), ("first", "next"),
                      ("first", "next"), ("second", "iter"), ("second", "next")]
    assert next(joined, "done") == "done"
    count = len(events)
    assert next(joined, "done") == "done"
    assert len(events) == count

    events = []
    inner = Traced([7], events, "inner")
    outer = Traced([inner], events, "outer")
    joined = chain.from_iterable(outer)
    assert events == [("outer", "iter")]
    assert next(joined) == 7
    assert events == [("outer", "iter"), ("outer", "next"),
                      ("inner", "iter"), ("inner", "next")]
    raises(TypeError, chain.from_iterable, 42)
    bad = chain([1], 42, [2])
    assert next(bad) == 1
    raises(TypeError, next, bad)
    assert next(bad, "done") == "done"


def test_chain_exhaustion():
    outer = ExhaustingOuter()
    joined = chain.from_iterable(outer)
    for attempt in (0, 1, 2):
        try:
            next(joined)
            assert False
        except StopIteration as caught:
            assert caught.args == ()
        assert outer.calls == 1

    # iter(inner) errors retain their arguments and still close the chain.
    joined = chain.from_iterable([ExhaustingInner(), [9]])
    try:
        next(joined)
        assert False
    except StopIteration as caught:
        assert caught.args == ("inner payload", 7)
    for attempt in (0, 1):
        try:
            next(joined)
            assert False
        except StopIteration as caught:
            assert caught.args == ()


def test_repeat():
    marker = []
    infinite = repeat(marker)
    assert iter(infinite) is infinite
    assert next(infinite) is marker
    assert next(infinite) is marker
    finite = repeat(object=marker, times=2)
    assert next(finite) is marker
    assert next(finite) is marker
    assert next(finite, "done") == "done"
    assert next(finite, "done") == "done"
    assert list(repeat(3, 0)) == []
    assert list(repeat(3, -2)) == []
    assert list(repeat(3, -(2**63))) == []
    assert list(repeat(3, False)) == []
    assert list(repeat(3, True)) == [3]
    assert next(repeat(4, 2**63 - 1)) == 4
    assert next(repeat(4, 2**62)) == 4
    raises(OverflowError, repeat, 1, 2**63)
    raises(OverflowError, repeat, 1, -(2**63) - 1)
    for value in (None, 1.5, "2", []):
        raises(TypeError, repeat, 1, value)


def test_islice():
    assert list(islice([], 3)) == []
    assert list(islice([1, 2, 3], None)) == [1, 2, 3]
    assert list(islice([1, 2, 3], None, None, None)) == [1, 2, 3]
    assert list(islice([1, 2, 3], True)) == [1]
    assert list(islice([1, 2, 3], False, True, True)) == [1]
    assert list(islice(repeat(5), 4)) == [5, 5, 5, 5]
    assert list(islice(chain(repeat(5), 42), 4)) == [5, 5, 5, 5]
    assert list(islice([0, 1, 2, 3, 4, 5], 1, 5, 2)) == [1, 3]
    assert list(islice([0, 1, 2], 2**62)) == [0, 1, 2]
    assert list(islice([0, 1, 2], 0, None, 2**63 - 1)) == [0]
    assert list(islice([0, 1, 2], 0, 2, 2**63 - 1)) == [0]

    # Exhausting islice consumes up to max(start, stop), independent of step.
    for case in (
        (0, 0, 1, [], 0), (4, 2, 1, [], 4), (2, 2, 3, [], 2),
        (0, 5, 3, [0, 3], 5), (1, 5, 10, [1], 5),
    ):
        start = case[0]
        stop = case[1]
        step = case[2]
        expected = case[3]
        consumed = case[4]
        source = iter([0, 1, 2, 3, 4, 5, 6])
        sliced = islice(source, start, stop, step)
        assert iter(sliced) is sliced
        assert list(sliced) == expected
        assert next(source) == consumed
        assert next(sliced, "done") == "done"
        assert next(source) == consumed + 1

    events = []
    source = Traced([0, 1, 2, 3, 4, 5], events, "source")
    sliced = islice(source, 1, 4, 2)
    assert events == [("source", "iter")]
    assert next(sliced) == 1
    assert len(events) == 3
    assert next(sliced) == 3
    assert len(events) == 5
    assert next(sliced, "done") == "done"
    assert len(events) == 5
    assert next(sliced, "done") == "done"
    assert len(events) == 5

    for value in (-1, 1.5, "2", [], 2**63):
        raises(ValueError, islice, [], value)
        raises(ValueError, islice, [], value, None)
        raises(ValueError, islice, [], 0, value)
    for value in (0, -1, 1.5, "2", [], 2**63):
        raises(ValueError, islice, [], 0, None, value)
    raises(TypeError, islice, 42, 0)


class ReentrantSource:
    def __init__(self):
        self.position = 0
        self.target = None
        self.nested_exhaustions = 0

    def __iter__(self):
        return self

    def __next__(self):
        position = self.position
        self.position += 1
        if position == 0:
            try:
                next(self.target)
            except StopIteration:
                self.nested_exhaustions += 1
        if position >= 2:
            raise StopIteration
        return position


def test_islice_reentrancy():
    # Cross-checked against CPython 3.14.5: nested exhaustion must not make
    # the outer skip loop read the slice's cleared iterator reference.
    # start=2 reaches the selected-item read; start=4 continues skipping.
    for start in (2, 4):
        for stop in (None, 5):
            source = ReentrantSource()
            sliced = islice(source, start, stop)
            source.target = sliced
            for attempt in (0, 1, 2):
                try:
                    next(sliced)
                    assert False
                except StopIteration as caught:
                    assert caught.args == ()
                assert source.position == 4
                assert source.nested_exhaustions == 1


def test_starmap():
    assert list(starmap(add, [])) == []
    assert list(starmap(add, [(1, 2), [3, 4], iter([5, 6])])) == [3, 7, 11]
    assert list(starmap(capture, [(), [], iter(["a", "b"])])) == [(), (), ("a", "b")]
    events = []
    rows = Traced([(2, 3), (4, 5)], events, "rows")
    mapped = starmap(add, rows)
    assert iter(mapped) is mapped
    assert events == [("rows", "iter")]
    assert next(mapped) == 5
    assert events == [("rows", "iter"), ("rows", "next")]
    assert next(mapped) == 9
    assert next(mapped, "done") == "done"
    assert next(mapped, "done") == "done"

    bad_rows = starmap(add, [42, (1,), (3, 4)])
    raises(TypeError, next, bad_rows)
    raises(TypeError, next, bad_rows)
    assert next(bad_rows) == 7
    assert list(starmap(42, [])) == []  # Callable validation is lazy too.
    raises(TypeError, next, starmap(42, [()]))
    raises(TypeError, starmap, add, 42)


def test_exceptions():
    for iterator in (chain(Recovering()), starmap(capture, Recovering())):
        try:
            next(iterator)
            assert False
        except ValueError as caught:
            assert caught is error
        assert next(iterator) == (7,)
        assert next(iterator, "done") == "done"

    for iterator in (chain.from_iterable(Recovering()), islice(Recovering(), 3),
                     islice(Recovering(), 1, 3)):
        try:
            next(iterator)
            assert False
        except ValueError as caught:
            assert caught is error
        assert next(iterator, "done") == "done"

    # An inner iter() failure closes chain; construction does not inspect it.
    joined = chain(BadIterable(), [9])
    try:
        next(joined)
        assert False
    except ValueError as caught:
        assert caught is error
    assert next(joined, "done") == "done"

    # Invalid bounds are rejected before islice asks the input for an iterator.
    raises(ValueError, islice, BadIterable(), -1)
    for case in (
        (chain.from_iterable, (BadIterable(),)),
        (islice, (BadIterable(), 0)),
        (starmap, (capture, BadIterable())),
    ):
        constructor = case[0]
        arguments = case[1]
        try:
            constructor(*arguments)
            assert False
        except ValueError as caught:
            assert caught is error

    row = Recovering()
    mapped = starmap(record, [row, (8,)])
    assert calls == []
    try:
        next(mapped)
        assert False
    except ValueError as caught:
        assert caught is error
    assert calls == []
    assert row.position == 1
    assert next(mapped) == (8,)
    assert calls == [(8,)]

    mapped = starmap(fail, [(1,), (2,)])
    for attempt in (0, 1):
        try:
            next(mapped)
            assert False
        except ValueError as caught:
            assert caught is error
    assert next(mapped, "done") == "done"

    mapped = starmap(stop, [(1,), (2,)])
    raises(StopIteration, next, mapped)
    raises(StopIteration, next, mapped)
    assert next(mapped, "done") == "done"


def test_calls():
    # Arity and keyword acceptance follow the supported CPython call surface.
    raises(TypeError, chain, iterable=[])
    raises(TypeError, chain.from_iterable)
    raises(TypeError, chain.from_iterable, [], [])
    raises(TypeError, chain.from_iterable, iterable=[])
    raises(TypeError, repeat)
    raises(TypeError, repeat, 1, 2, 3)
    raises(TypeError, repeat, 1, object=2)
    raises(TypeError, repeat, 1, unknown=2)
    raises(TypeError, islice)
    raises(TypeError, islice, [])
    raises(TypeError, islice, [], 0, 1, 2, 3)
    raises(TypeError, islice, [], stop=1)
    raises(TypeError, starmap)
    raises(TypeError, starmap, add)
    raises(TypeError, starmap, add, [], [])
    raises(TypeError, starmap, function=add, iterable=[])


def test_integer_subclasses():
    assert list(repeat(1, Integer(2))) == [1, 1]
    assert list(islice([0, 1, 2], Integer(1), Integer(3), Integer(1))) == [1, 2]


# Custom __index__, callable instances, instance access to from_iterable, and
# subclass-preserving from_iterable construction are intentionally deferred.

test_chain()
test_chain_exhaustion()
test_repeat()
test_islice()
test_islice_reentrancy()
test_starmap()
test_exceptions()
test_calls()
test_integer_subclasses()
