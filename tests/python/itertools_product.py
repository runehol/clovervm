# Python test set -- itertools product
#
# Portions adapted from CPython's Lib/test/test_itertools.py at v3.14.10.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import itertools
import sys
from itertools import product


def raises(kind, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as error:
        return error
    raise AssertionError('expected exception')


class Source:
    def __init__(self, name, values, events):
        self.name = name
        self.values = iter(values)
        self.events = events

    def __iter__(self):
        self.events.append(('iter', self.name))
        return self

    def __next__(self):
        self.events.append(('next', self.name))
        item = next(self.values)
        if isinstance(item, BaseException):
            raise item
        return item


def test_values_and_calls():
    if hasattr(itertools, '__all__'):
        assert 'product' in itertools.__all__
        # The managed module explicitly excludes general __index__ dispatch.
        # Host execution uses the host operator without replacing it.
        if 'clovervm' in sys.version:
            raises(TypeError, product, [1], repeat=CustomIndex())
    assert product is itertools.product
    assert list(product()) == [()]
    assert list(product(repeat=0)) == [()]
    assert list(product(repeat=2**63 - 1)) == [()]
    assert list(product('ab')) == [('a',), ('b',)]
    assert list(product('ab', [1, 2])) == [('a', 1), ('a', 2), ('b', 1), ('b', 2)]
    assert list(product([1, 2], repeat=2)) == [(1, 1), (1, 2), (2, 1), (2, 2)]
    assert list(product([1], ['a'], repeat=2)) == [(1, 'a', 1, 'a')]
    assert list(product([], [1])) == []
    assert list(product([1], [], [2])) == []
    assert list(product([1], [])) == []
    assert list(product(42, None, repeat=False)) == [()]
    assert list(product([1, 2], repeat=True)) == [(1,), (2,)]
    assert list(product({'a': 1, 'b': 2})) == [('a',), ('b',)]
    assert list(product(b'ab')) == [(97,), (98,)]
    assert list(product(bytearray(b'ab'))) == [(97,), (98,)]
    raises(TypeError, product, 42)
    raises(TypeError, product, [1], 2)
    raises(TypeError, product, iterable=[])
    raises(TypeError, product, [1], repeat=0, unknown=1)
    for repeat in (None, '2', 2.0):
        raises(TypeError, product, repeat=repeat)
    for repeat in (-1, -(2**63)):
        raises(ValueError, product, repeat=repeat)
    for repeat in (2**63, -(2**63) - 1):
        raises(OverflowError, product, repeat=repeat)
    # Both index-array multiplication bounds fail before any allocation/input.
    raises(OverflowError, product, 42, repeat=2**60)
    raises(OverflowError, product, 42, 43, repeat=2**59)


class IntSubclass(int):
    def __index__(self):
        raise AssertionError('must use integer payload')

    def __int__(self):
        raise AssertionError('must use integer payload')


class CustomIndex:
    def __index__(self):
        raise AssertionError('custom index is outside this slice')


class TupleSubclass(tuple):
    def __iter__(self):
        return iter([9])


class BadAcquisition:
    def __init__(self, error, events):
        self.error = error
        self.events = events

    def __iter__(self):
        self.events.append('bad iter')
        raise self.error


class MutatingSource:
    def __init__(self, later):
        self.later = later
        self.done = False

    def __iter__(self):
        return self

    def __next__(self):
        if self.done:
            raise StopIteration
        self.done = True
        self.later.append(2)
        return 9


def test_construction_order():
    assert list(product([7], repeat=IntSubclass(2))) == [(7, 7)]
    assert list(product(TupleSubclass((1, 2)))) == [(9,)]
    events = []
    first = Source('first', [1, 2], events)
    second = Source('second', [3], events)
    selected = product(first, second, repeat=2)
    expected = [('iter', 'first'), ('next', 'first'), ('next', 'first'),
                ('next', 'first'), ('iter', 'second'), ('next', 'second'),
                ('next', 'second')]
    assert events == expected
    assert list(selected) == [(1, 3, 1, 3), (1, 3, 2, 3),
                              (2, 3, 1, 3), (2, 3, 2, 3)]
    assert events == expected
    events = []
    error = ValueError('acquisition')
    bad = BadAcquisition(error, events)
    raises(TypeError, product, bad, repeat=CustomIndex(), unknown=1)
    assert events == []
    assert list(product(bad, repeat=0)) == [()]
    assert events == []
    for case in ((-1, ValueError), (2**63, OverflowError), (2.0, TypeError),
                 (2**60, OverflowError)):
        raises(case[1], product, bad, repeat=case[0])
        assert events == []
    assert raises(ValueError, product, [], bad, Source('skipped', [], events)) is error
    assert events == ['bad iter']
    events = []
    error = RuntimeError('next')
    first = Source('first', [1, error, 2], events)
    assert raises(RuntimeError, product, first, Source('skipped', [], events)) is error
    assert events == [('iter', 'first'), ('next', 'first'), ('next', 'first')]
    assert next(first) == 2
    events = []
    source = Source('shared', [1, 2], events)
    assert list(product(source, source)) == []
    assert events == [('iter', 'shared'), ('next', 'shared'), ('next', 'shared'),
                      ('next', 'shared'), ('iter', 'shared'), ('next', 'shared')]
    events = []
    source = Source('pause', [StopIteration('pause'), 9], events)
    assert list(product(source)) == []
    assert next(source) == 9
    later = [1]
    assert list(product(MutatingSource(later), later)) == [(9, 1), (9, 2)]


def test_identity_and_snapshots():
    item = []
    source = [item, None]
    selected = product(source, repeat=2)
    source.clear()
    first = next(selected)
    assert first.__class__ is tuple
    assert first[0] is item and first[1] is item
    item.append(7)
    assert first == ([7], [7])
    assert next(selected) == ([7], None)
    assert first == ([7], [7])
    assert next(selected) == (None, [7])
    assert next(selected) == (None, None)
    assert next(selected, 'done') == 'done'
    assert next(selected, 'done') == 'done'
    # A huge Cartesian result is never materialized; only its small pools are.
    selected = product([item, None], repeat=40)
    row = next(selected)
    assert len(row) == 40
    for value in row:
        assert value is item
    row = next(selected)
    assert row[-1] is None and row[-2] is item


def test_reinitialization_and_reentry():
    selected = product([1, 2])
    assert iter(selected) is selected
    assert next(selected) == (1,)
    error = ValueError('must not acquire')
    events = []
    assert selected.__init__(BadAcquisition(error, events), repeat=-1, unknown=1) is None
    assert selected.__init__(self=42) is None
    assert events == []
    assert list(selected) == [(2,)]
    selected.__init__([9])
    assert next(selected, 'done') == 'done'
    # Construction callbacks may advance an existing product. Reinitializing
    # that existing product must neither consume the argument nor reset it.
    global target, nested
    target = product([4, 5, 6])
    nested = []
    outer = product(ReentrantSource())
    assert nested == [(4,), (5,)]
    assert list(outer) == [(9,)]
    assert list(target) == [(6,)]
    target = None


target = None
nested = []


class ReentrantSource:
    def __init__(self):
        self.done = False

    def __iter__(self):
        nested.append(next(target))
        target.__init__(42, repeat=-1)
        return self

    def __next__(self):
        if self.done:
            raise StopIteration
        self.done = True
        nested.append(next(target))
        return 9


class MutableSource:
    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration


def one_then_stop(self):
    if hasattr(self, 'done'):
        raise StopIteration
    self.done = True
    return 8


def test_cache_and_hot_iteration():
    for _ in range(300):
        assert list(product(MutableSource())) == []
    MutableSource.__next__ = one_then_stop
    for _ in range(300):
        assert list(product(MutableSource())) == [(8,)]
    selected = product(range(25), range(25))
    index = 0
    for row in selected:
        assert row.__class__ is tuple
        assert row == (index // 25, index % 25)
        index += 1
    assert index == 625
    assert next(selected, 'done') == 'done'


def make_unretained_product():
    item = [7]
    # No source or external item reference survives this function.
    return product([item], range(300))


def test_pool_and_result_roots():
    selected = make_unretained_product()
    first = next(selected)
    first[0].append(8)
    for index in range(1, 300):
        row = next(selected)
        assert row[0] is first[0]
        assert row == ([7, 8], index)
        assert first == ([7, 8], 0)
    assert next(selected, 'done') == 'done'
    selected = None
    # An independently retained result survives wrapper/pool reclamation.
    for _ in range(30):
        assert list(product([1, 2], [3])) == [(1, 3), (2, 3)]
    assert first == ([7, 8], 0)


test_values_and_calls()
test_construction_order()
test_identity_and_snapshots()
test_reinitialization_and_reentry()
test_cache_and_hot_iteration()
test_pool_and_result_roots()
