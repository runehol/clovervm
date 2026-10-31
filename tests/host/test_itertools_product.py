"""Compare production managed product with native CPython 3.14.10.

Run with python3.14. No production helpers are replaced. The host operator.index
supports custom indices; CloverVM's documented builtin-index boundary is tested
only in the interpreter. Tuple recycling, allocation-failure timing, and exact
refcount-sensitive finalization timing are excluded.
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
shared = compile((root / 'tests/python/itertools_product.py').read_text(),
                 'tests/python/itertools_product.py', 'exec')
exec(shared, {})
sys.modules['itertools'] = managed
try:
    exec(shared, {})
finally:
    sys.modules['itertools'] = itertools


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


def observe(factory, streams, repeat, aliases=False):
    events = []
    sources = [Source(n, values, events) for n, values in enumerate(streams)]
    if aliases:
        sources = [sources[0], sources[0]]
    try:
        selected = factory(*sources, repeat=repeat)
    except BaseException as error:
        outcome = ('error', type(error), error.args)
    else:
        rows = list(selected)
        assert all(type(row) is tuple for row in rows)
        outcome = ('rows', rows, next(selected, 'done'), next(selected, 'done'))
    construction_events = list(events)
    # Record resumable tails too, so a spurious extra next() is observable.
    tails = []
    for source in sources:
        tail = []
        for _ in range(3):
            try:
                tail.append(('item', next(source)))
            except BaseException as error:
                tail.append(('error', type(error), error.args))
        tails.append(tail)
    return outcome, construction_events, tails


streams = [()]
for length in (1, 2):
    streams.extend(itertools.product((None, 0, StopIteration('pause'), ValueError('input')), repeat=length))
cases = 0
for count in range(4):
    for inputs in itertools.product(streams, repeat=count):
        for repeat in (0, 1, 2):
            expected = observe(itertools.product, inputs, repeat)
            actual = observe(managed.product, inputs, repeat)
            assert actual == expected, (inputs, repeat, actual, expected)
            cases += 1
for stream in streams:
    for repeat in (0, 1, 2):
        assert observe(managed.product, [stream], repeat, True) == observe(itertools.product, [stream], repeat, True)
        cases += 1


class Payload:
    pass


class FailingAcquisition:
    def __iter__(self):
        raise ValueError('acquisition')


class FreshPayload:
    def __init__(self, refs):
        self.refs = refs
        self.done = False

    def __iter__(self):
        return self

    def __next__(self):
        if self.done:
            raise StopIteration
        self.done = True
        item = Payload()
        self.refs.append(weakref.ref(item))
        return item


for module in (itertools, managed):
    for empty in (False, True):
        item = Payload()
        item_ref = weakref.ref(item)
        source = Source('source', [item], [])
        source_ref = weakref.ref(source)
        inputs = (source, []) if empty else (source,)
        selected = module.product(*inputs, repeat=2)
        del source, item, inputs
        gc.collect()
        assert source_ref() is None  # Only materialized pools are retained.
        assert item_ref() is not None
        rows = list(selected)
        del rows
        gc.collect()
        assert item_ref() is not None  # Even an empty product retains its pools.
        assert next(selected, 'done') == 'done'
        selected.__init__([], repeat=-1)
        gc.collect()
        assert item_ref() is not None  # No-op reinit retains exhausted state.
        del selected
        gc.collect()
        assert item_ref() is None

    refs = []
    try:
        module.product(FreshPayload(refs), FailingAcquisition())
    except ValueError:
        pass
    else:
        raise AssertionError('missing acquisition error')
    gc.collect()
    assert len(refs) == 1 and refs[0]() is None  # Release partial pools on failure.

    item = Payload()
    item_ref = weakref.ref(item)
    selected = module.product([item], repeat=2)
    row = next(selected)
    del item, selected
    gc.collect()
    assert row[0] is item_ref() and row[1] is item_ref()
    del row
    gc.collect()
    assert item_ref() is None  # Returned tuples own their elements independently.

print(f'product: {cases} differential consumption cases; shared assertions and lifetime checks passed on {sys.version.split()[0]}')
