"""Compare the unmodified managed cycle with CPython 3.14.10.

Run with python3.14. Exact refcount-sensitive finalization timing, allocation
failure timing, subclass fidelity, and copy/pickle/metadata are excluded.
"""

import gc
import importlib.util
import itertools
from pathlib import Path
import sys
import weakref


root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('clover_itertools', root / 'stdlib/itertools.py')
managed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(managed)
shared = compile((root / 'tests/python/itertools_cycle.py').read_text(),
                 'tests/python/itertools_cycle.py', 'exec')
exec(shared, {})
sys.modules['itertools'] = managed
try:
    exec(shared, {})
finally:
    sys.modules['itertools'] = itertools


class Source:
    def __init__(self, events, values, reenter=-1, mode=0):
        self.events = events
        self.values = iter(values)
        self.calls = 0
        self.reenter = reenter
        self.mode = mode

    def __iter__(self):
        self.events.append('iter')
        return self

    def __next__(self):
        self.calls += 1
        self.events.append(('next', self.calls))
        if self.calls == self.reenter:
            nested = next(self.target, 'empty')
            self.events.append(('nested', nested))
            if self.mode == 1:
                return 99
            if self.mode == 2:
                raise ValueError('outer')
            if self.mode == 3:
                raise StopIteration('outer')
            return nested
        value = next(self.values)
        if isinstance(value, Exception):
            raise value
        return value


def observe(factory, values, reenter, mode):
    events = []
    source = Source(events, values, reenter, mode)
    target = factory(source)
    source.target = target
    events.append(('constructed', source.calls))
    results = []
    for _ in range(10):
        try:
            results.append(('value', next(target)))
        except Exception as error:
            results.append(('error', type(error), error.args))
    # Any resumable tail is still owned by the original source, not the cycle.
    source.target = iter(())
    for _ in range(2):
        try:
            results.append(('tail', next(source)))
        except Exception as error:
            results.append(('tail-error', type(error), error.args))
    return results, events


cases = 0
for length in range(5):
    streams = itertools.product((0, None, ValueError('source'), StopIteration('pause')),
                                repeat=length)
    for values in streams:
        for reenter in (-1, 1, 2, 3):
            for mode in range(4):
                expected = observe(itertools.cycle, values, reenter, mode)
                actual = observe(managed.cycle, values, reenter, mode)
                assert actual == expected, (values, reenter, mode, actual, expected)
                cases += 1


class Payload:
    pass


class Fresh:
    def __init__(self):
        self.done = False

    def __iter__(self):
        return self

    def __next__(self):
        if self.done:
            raise StopIteration
        self.done = True
        return Payload()


class Empty:
    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration('ignored')


class Failure:
    def __iter__(self):
        raise ValueError('acquisition')


for factory in (itertools.cycle, managed.cycle):
    source = Fresh()
    source_ref = weakref.ref(source)
    selected = factory(source)
    del source
    gc.collect()
    assert source_ref() is not None
    item = next(selected)
    item_ref = weakref.ref(item)
    del item
    gc.collect()
    assert item_ref() is not None and source_ref() is not None
    item = next(selected)
    gc.collect()
    assert source_ref() is None and item is item_ref()
    del item, selected
    gc.collect()
    assert item_ref() is None

    # Deletion before exhaustion must also release both source and cache.
    source = Fresh()
    source_ref = weakref.ref(source)
    selected = factory(source)
    item = next(selected)
    item_ref = weakref.ref(item)
    del item, source, selected
    gc.collect()
    assert item_ref() is None and source_ref() is None

    source = Empty()
    source_ref = weakref.ref(source)
    selected = factory(source)
    del source
    gc.collect()
    assert source_ref() is not None
    assert next(selected, 'done') == 'done'
    gc.collect()
    assert source_ref() is None
    assert next(selected, 'done') == 'done'

    source = Failure()
    source_ref = weakref.ref(source)
    try:
        factory(source)
    except ValueError:
        pass
    else:
        raise AssertionError('acquisition must fail')
    del source
    gc.collect()
    assert source_ref() is None

print(cases, 'differential cases; shared semantics and lifetime checks passed')
