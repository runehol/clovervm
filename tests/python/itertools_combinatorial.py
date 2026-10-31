# Python test set -- itertools combinatorial iterators
#
# Portions adapted from CPython's Lib/test/test_itertools.py at v3.14.10.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import itertools
import sys
from itertools import combinations, combinations_with_replacement, permutations


factories = (combinations, combinations_with_replacement, permutations)


def raises(kind, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as error:
        return error
    raise AssertionError('expected exception')


class IntSubclass(int):
    def __index__(self):
        raise AssertionError('index override')

    def __int__(self):
        raise AssertionError('int override')


class CustomIndex:
    def __index__(self):
        raise AssertionError('custom index is outside this slice')


class Source:
    def __init__(self, values, events):
        self.values = iter(values)
        self.events = events

    def __iter__(self):
        self.events.append('iter')
        return self

    def __next__(self):
        self.events.append('next')
        item = next(self.values)
        if isinstance(item, BaseException):
            raise item
        return item


class BadAcquisition:
    def __init__(self, error, events):
        self.error = error
        self.events = events

    def __iter__(self):
        self.events.append('bad iter')
        raise self.error


class TupleSubclass(tuple):
    def __iter__(self):
        return iter([8, 9])


class Uncomparable:
    def __eq__(self, other):
        raise AssertionError('must not compare pool elements')

    def __lt__(self, other):
        raise AssertionError('must not sort pool elements')


class Keyword(str):
    def __ne__(self, other):
        raise AssertionError('keyword validation must not call __ne__')


def test_values_and_calls():
    assert list(combinations('abcd', 2)) == [
        ('a', 'b'), ('a', 'c'), ('a', 'd'), ('b', 'c'), ('b', 'd'), ('c', 'd')]
    assert list(combinations_with_replacement('abc', 2)) == [
        ('a', 'a'), ('a', 'b'), ('a', 'c'), ('b', 'b'), ('b', 'c'), ('c', 'c')]
    assert list(permutations('abc', 2)) == [
        ('a', 'b'), ('a', 'c'), ('b', 'a'), ('b', 'c'), ('c', 'a'), ('c', 'b')]
    assert list(permutations([0, 1, 2])) == [
        (0, 1, 2), (0, 2, 1), (1, 0, 2), (1, 2, 0), (2, 0, 1), (2, 1, 0)]
    assert list(permutations([0, 1], None)) == [(0, 1), (1, 0)]
    assert list(permutations(iterable=[])) == [()]
    assert list(combinations('aa', 2)) == [('a', 'a')]
    assert list(combinations_with_replacement('aa', 2)) == [
        ('a', 'a'), ('a', 'a'), ('a', 'a')]
    assert list(permutations('aa', 2)) == [('a', 'a'), ('a', 'a')]
    assert list(combinations([1, 2], 3)) == []
    assert list(permutations([1, 2], 3)) == []
    # Empty outputs need no enormous index/cycle arrays in this implementation.
    # Allocation timing is deliberately excluded, so host differentials use
    # small r and the deterministic PyMem_New overflow boundary instead.
    if 'clovervm' in sys.version:
        assert list(combinations([1], 2**50)) == []
        assert list(permutations([1], 2**50)) == []
        assert list(combinations_with_replacement([], 2**50)) == []
    for factory in factories:
        if hasattr(itertools, '__all__'):
            assert factory.__name__ in itertools.__all__
        assert list(factory([], 0)) == [()]
        assert list(factory([], 1)) == []
        assert list(factory([1, 2], False)) == [()]
        assert list(factory([1, 2], True)) == [(1,), (2,)]
        assert list(factory([1, 2], IntSubclass(1))) == [(1,), (2,)]
        assert list(factory(TupleSubclass((1,)), 1)) == [(8,), (9,)]
        assert list(factory(b'ab', 1)) == [(97,), (98,)]
        assert list(factory(bytearray(b'ab'), 1)) == [(97,), (98,)]
        assert list(factory({'a': 1, 'b': 2}, 1)) == [('a',), ('b',)]
        assert list(factory(iterable=[1, 2], r=1)) == [(1,), (2,)]
        assert list(factory([1, 2], r=1)) == [(1,), (2,)]
        assert list(factory(**{Keyword('iterable'): [1, 2], Keyword('r'): 1})) == [(1,), (2,)]
        raises(TypeError, factory)
        raises(TypeError, factory, [1], 1, 2)
        raises(TypeError, factory, [1], 1, r=1)
        raises(TypeError, factory, [1], 1, iterable=[2])
        raises(TypeError, factory, [1], 1, unknown=1)
        raises(TypeError, factory, r=1)
        raises(TypeError, factory, self=[1], r=1)
        raises(TypeError, factory, 42, 0)
        raises(TypeError, factory, [1], 1.0)
        raises(TypeError, factory, [1], '1')
        raises(ValueError, factory, [1], -1)
        raises(ValueError, factory, [1], -(2**63))
        raises(OverflowError, factory, [1], 2**63)
        raises(OverflowError, factory, [1], -(2**63) - 1)
        raises(MemoryError, factory, [1], 2**60)
        if 'clovervm' in sys.version:
            raises(TypeError, factory, [1], CustomIndex())
    raises(TypeError, combinations, [1])
    raises(TypeError, combinations_with_replacement, [1])
    raises(TypeError, combinations, [1], None)
    raises(TypeError, combinations_with_replacement, [1], None)


def test_construction_order():
    for factory in factories:
        events = []
        selected = factory(Source([1, 2], events), 0)
        assert events == ['iter', 'next', 'next', 'next']
        assert list(selected) == [()]
        assert events == ['iter', 'next', 'next', 'next']
        for case in ((1.0, TypeError), (2**63, OverflowError),
                     (-(2**63) - 1, OverflowError)):
            r = case[0]
            kind = case[1]
            events = []
            source = Source([1], events)
            raises(kind, factory, source, r)
            if factory is permutations:
                assert events == ['iter', 'next', 'next']
            else:
                assert events == []
        for case in ((-1, ValueError), (2**60, MemoryError)):
            r = case[0]
            kind = case[1]
            events = []
            raises(kind, factory, Source([1], events), r)
            assert events == ['iter', 'next', 'next']
        events = []
        error = RuntimeError('acquisition')
        assert raises(RuntimeError, factory, BadAcquisition(error, events), -1) is error
        assert events == ['bad iter']
        events = []
        error = ValueError('source')
        source = Source([1, error, 9], events)
        assert raises(ValueError, factory, source, 0) is error
        assert events == ['iter', 'next', 'next']
        assert next(source) == 9
        events = []
        source = Source([StopIteration('pause'), 9], events)
        assert list(factory(source, 1)) == []
        assert next(source) == 9
        events = []
        raises(TypeError, factory, Source([1], events), 1, unknown=1)
        assert events == []


def test_snapshots_and_identity():
    for factory in factories:
        a = []
        b = []
        source = [a, b]
        selected = factory(source, 1)
        source.clear()
        first = next(selected)
        assert first.__class__ is tuple
        assert first[0] is a
        a.append(7)
        second = next(selected)
        assert first is not second
        assert second[0] is b
        assert first == ([7],)
        assert iter(selected) is selected
        assert next(selected, 'done') == 'done'
        assert next(selected, 'done') == 'done'
        # Equal objects occupy different positions without invoking equality.
        rows = list(factory([a, a, b], 2))
        assert rows[0][0] is a and rows[0][1] is a
        assert rows[-1][-1] is b or factory is permutations
        assert rows[0].__class__ is tuple
        a = Uncomparable()
        b = Uncomparable()
        rows = list(factory([a, b], 2))
        assert rows[0][0] is a
        if factory is combinations_with_replacement:
            assert rows[0][1] is a
            assert len(rows) == 3
        else:
            assert rows[0][1] is b
            if factory is permutations:
                assert len(rows) == 2
                assert rows[1][0] is b and rows[1][1] is a
            else:
                assert len(rows) == 1
    # Large result sets must remain lazy; constructor state stays bounded.
    assert next(combinations(range(100), 50)) == tuple(range(50))
    row = next(combinations_with_replacement(range(20), 100))
    assert len(row) == 100
    for item in row:
        assert item == 0
    assert next(permutations(range(100), 50)) == tuple(range(50))


target = None
nested = []


class ReentrantSource:
    def __init__(self):
        self.done = False

    def __iter__(self):
        nested.append(next(target))
        target.__init__(42, -1, unknown=True)
        return self

    def __next__(self):
        if self.done:
            raise StopIteration
        self.done = True
        nested.append(next(target))
        return [9]


def test_reentry_and_reinitialization():
    global target, nested
    for factory in factories:
        selected = factory([1, 2], 1)
        assert next(selected) == (1,)
        events = []
        assert selected.__init__(BadAcquisition(ValueError(), events), -1,
                                 unknown=1) is None
        assert selected.__init__() is None
        assert selected.__init__(self=42) is None
        assert events == []
        assert list(selected) == [(2,)]
        selected.__init__([9], 1)
        assert next(selected, 'done') == 'done'
        target = factory([4, 5, 6], 1)
        nested = []
        outer = factory(ReentrantSource(), 1)
        assert nested == [(4,), (5,)]
        assert list(outer) == [([9],)]
        assert list(target) == [(6,)]
        target = None


class MutableSource:
    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration


def one_then_stop(self):
    if hasattr(self, 'done'):
        raise StopIteration
    self.done = True
    return [8]


def make_unretained(factory):
    return factory([[7], [8], [9], [10], [11], [12]], 3)


def test_roots_caches_and_hot_iteration():
    for factory in factories:
        for _ in range(200):
            assert list(factory(MutableSource(), 1)) == []
    MutableSource.__next__ = one_then_stop
    for factory in factories:
        for _ in range(200):
            assert list(factory(MutableSource(), 1)) == [([8],)]
        selected = make_unretained(factory)
        first = next(selected)
        first[0].append(99)
        count = 1
        for row in selected:
            assert row.__class__ is tuple
            assert len(row) == 3
            assert first[0] == [7, 99]
            count += 1
        if factory is combinations:
            assert count == 20
        elif factory is combinations_with_replacement:
            assert count == 56
        else:
            assert count == 120
        selected = None
        for _ in range(20):
            assert list(factory([[1]], 1)) == [([1],)]
        assert first[0] == [7, 99]


test_values_and_calls()
test_construction_order()
test_snapshots_and_identity()
test_reentry_and_reinitialization()
test_roots_caches_and_hot_iteration()
