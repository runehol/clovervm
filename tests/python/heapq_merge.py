# Python test set -- heapq managed merge
# Portions adapted from CPython's Lib/test/test_heapq.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
import heapq


def terminal(iterator):
    for unused in range(2):
        try:
            next(iterator)
            assert False
        except StopIteration:
            pass
    if hasattr(heapq, '_MergeIterator'):
        assert iterator._iterables is None
        assert iterator._heap is None
        assert iterator._key is None
        assert iterator._reverse is None
        assert iterator._tail is None
        assert iterator._heapify is None
        assert iterator._heappop is None
        assert iterator._heapreplace is None


def test_results():
    assert list(heapq.merge()) == []
    assert list(heapq.merge([], [], key=42)) == []
    assert list(heapq.merge([1, 3, 5], [], [2, 4, 6])) == [1, 2, 3, 4, 5, 6]
    assert list(heapq.merge([5, 3, 1], [6, 4, 2], reverse=True)) == [6, 5, 4, 3, 2, 1]
    def key(row):
        return row[0]
    for reverse in (False, True):
        a = [(1, 'a'), (1, 'b')]
        b = [(1, 'c'), (1, 'd')]
        assert list(heapq.merge(a, b, key=key, reverse=reverse)) == a + b
    iterator = heapq.merge([1])
    assert iter(iterator) is iterator
    assert next(iterator) == 1
    terminal(iterator)
    try:
        heapq.merge([], unknown=True)
        assert False
    except TypeError:
        pass


def test_unkeyed_tie_identity():
    class Entry:
        def __init__(self, value, label):
            self.value = value
            self.label = label
        def __eq__(self, other):
            return self.value == other.value
        def __lt__(self, other):
            return self.value < other.value
    a = Entry(1, 'a')
    b = Entry(1, 'b')
    c = Entry(1, 'c')
    for reverse in (False, True):
        iterator = heapq.merge([a, b], [c], reverse=reverse)
        assert next(iterator) is a
        assert next(iterator) is b
        assert next(iterator) is c
        terminal(iterator)


def test_traces():
    global log
    log = []
    class Source:
        def __init__(self, name, values):
            self.name = name
            self.values = iter(values)
        def __iter__(self):
            log.append(('iter', self.name))
            return self
        def __next__(self):
            log.append(('next', self.name))
            return next(self.values)
    class Reverse:
        def __bool__(self):
            log.append('reverse')
            return False
    def key(x):
        log.append(('key', x))
        return x
    iterator = heapq.merge(Source('a', [1, 3]), Source('b', [2, 4, 5]),
                           key=key, reverse=Reverse())
    assert log == []
    assert next(iterator) == 1
    assert log == ['reverse', ('iter', 'a'), ('next', 'a'), ('key', 1),
                   ('iter', 'b'), ('next', 'b'), ('key', 2)]
    log.clear()
    assert next(iterator) == 2
    assert log == [('next', 'a'), ('key', 3)]
    log.clear()
    assert next(iterator) == 3
    assert log == [('next', 'b'), ('key', 4)]
    log.clear()
    assert next(iterator) == 4
    assert log == [('next', 'a')]
    log.clear()
    assert next(iterator) == 5
    assert log == [('iter', 'b'), ('next', 'b')]
    terminal(iterator)
    assert log == [('iter', 'b'), ('next', 'b'), ('next', 'b')]


def test_errors():
    global error, stop_at
    error = ValueError('source')
    class Source:
        def __iter__(self):
            return self
        def __next__(self):
            raise error
    iterator = heapq.merge(Source())
    try:
        next(iterator)
        assert False
    except ValueError as caught:
        assert caught is error
    terminal(iterator)
    class BadIter:
        def __iter__(self):
            raise StopIteration
    iterator = heapq.merge([1], BadIter(), [2])
    assert list(iterator) == [1]
    terminal(iterator)
    class BadReverse:
        def __bool__(self):
            raise StopIteration
    iterator = heapq.merge(reverse=BadReverse())
    try:
        next(iterator)
        assert False
    except RuntimeError as caught:
        assert str(caught) == 'generator raised StopIteration'
    terminal(iterator)
    for stop_at in (1, 3):
        def key(x):
            if x == stop_at:
                raise StopIteration
            return x
        iterator = heapq.merge([1, 3, 5], [2, 4, 6], key=key)
        expected = [2, 4, 6]
        if stop_at == 3:
            expected = [1, 2, 4, 6]
        assert list(iterator) == expected
        terminal(iterator)
    class Compare:
        def __eq__(self, other):
            return False
        def __lt__(self, other):
            raise StopIteration
    iterator = heapq.merge([Compare()], [Compare()])
    try:
        next(iterator)
        assert False
    except RuntimeError:
        pass
    terminal(iterator)
    iterator = heapq.merge(42)
    try:
        next(iterator)
        assert False
    except TypeError:
        pass
    terminal(iterator)


def test_reentry():
    global catch, seen, iterator
    for catch in (False, True):
        seen = []
        def key(value):
            try:
                next(iterator)
            except ValueError as error:
                assert str(error) == 'generator already executing'
                seen.append(value)
                if not catch:
                    raise
            return value
        iterator = heapq.merge([1, 2], key=key)
        if catch:
            assert list(iterator) == [1, 2]
        else:
            try:
                next(iterator)
                assert False
            except ValueError:
                pass
        assert seen == [1]
        terminal(iterator)


def test_tail_acquisition():
    class Source:
        def __init__(self):
            self.calls = 0
        def __iter__(self):
            self.calls += 1
            if self.calls == 2:
                raise StopIteration
            return self
        def __next__(self):
            return 1
    iterator = heapq.merge(Source())
    assert next(iterator) == 1
    try:
        next(iterator)
        assert False
    except RuntimeError:
        pass
    terminal(iterator)


def test_later_failures():
    global failure
    class Source:
        def __init__(self, values):
            self.values = iter(values)
        def __iter__(self):
            return self
        def __next__(self):
            value = next(self.values)
            if isinstance(value, BaseException):
                raise value
            return value
    def key(value):
        if value == 3:
            raise failure
        return value
    for failure in (ValueError('later'), KeyboardInterrupt('later')):
        for mode in ('key', 'source', 'tail'):
            if mode == 'key':
                iterator = heapq.merge([1, 3], [2, 4], key=key)
            elif mode == 'source':
                iterator = heapq.merge(Source([1, failure]), [2, 4])
            else:
                iterator = heapq.merge(Source([1, failure]))
            assert next(iterator) == 1
            try:
                next(iterator)
                assert False
            except BaseException as caught:
                assert caught is failure
            terminal(iterator)


def test_pop_comparison_failure():
    global pop_failure
    class Entry:
        def __init__(self, value):
            self.value = value
        def __eq__(self, other):
            return self.value == other.value
        def __lt__(self, other):
            if pop_failure is not None:
                raise pop_failure
            return self.value < other.value
    for error in (StopIteration('pop'), ValueError('pop')):
        pop_failure = None
        first = Entry(0)
        iterator = heapq.merge([first], [Entry(1)], [Entry(2)])
        assert next(iterator) is first
        pop_failure = error
        try:
            next(iterator)
            assert False
        except RuntimeError as caught:
            assert isinstance(error, StopIteration)
            assert str(caught) == 'generator raised StopIteration'
        except ValueError as caught:
            assert caught is error
        terminal(iterator)
    pop_failure = None


def test_hot_and_retention():
    # No external owners of source containers: exercise managed roots through
    # setup, repeated cached dispatch, tail transitions and completion.
    class Source:
        def __init__(self, values):
            self.values = iter(values)
        def __iter__(self):
            return self
        def __next__(self):
            return next(self.values)
    def identity(value):
        return value
    iterator = heapq.merge(Source(range(0, 600, 2)), Source(range(1, 600, 2)),
                           key=identity)
    for value in range(600):
        assert next(iterator) == value
    terminal(iterator)


test_results()
test_unkeyed_tie_identity()
test_traces()
test_errors()
test_reentry()
test_tail_acquisition()
test_hot_and_retention()
test_later_failures()

test_pop_comparison_failure()
