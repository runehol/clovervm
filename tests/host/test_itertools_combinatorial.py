"""Compare unmodified managed combinatorial iterators with CPython 3.14.10.

Run with python3.14. General index dispatch is unavailable in the VM; this host
uses real operator.index and exercises that VM boundary in the shared VM tests.
Length hints, tuple recycling, allocation/finalizer timing, and metadata fidelity
are excluded. No production helpers are replaced.
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
shared = compile((root / 'tests/python/itertools_combinatorial.py').read_text(),
                 'tests/python/itertools_combinatorial.py', 'exec')
exec(shared, {})
sys.modules['itertools'] = managed
try:
    exec(shared, {})
finally:
    sys.modules['itertools'] = itertools


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


def observe(factory, values, r):
    events = []
    source = Source(values, events)
    try:
        selected = factory(source, r)
    except BaseException as error:
        # Managed call/type diagnostics deliberately differ in wording.
        outcome = ('error', type(error))
        if isinstance(error, RuntimeError):
            outcome += (error.args,)
    else:
        rows = list(selected)
        assert all(type(row) is tuple for row in rows)
        outcome = ('rows', rows, next(selected, 'done'), next(selected, 'done'))
        # Invalid reinitialization never consumes its arguments or resets state.
        assert selected.__init__(source, -1, unknown=1) is None
        assert next(selected, 'done') == 'done'
    construction_events = list(events)
    tails = []
    for _ in range(4):
        try:
            tails.append(('item', next(source)))
        except BaseException as error:
            tails.append(('error', type(error), error.args))
    return outcome, construction_events, tails


cases = 0
streams = [()]
for length in (1, 2, 3):
    streams.extend(itertools.product((None, 0, StopIteration('pause'), RuntimeError('input')),
                                     repeat=length))
for name in ('combinations', 'combinations_with_replacement', 'permutations'):
    native = getattr(itertools, name)
    production = getattr(managed, name)
    for values in streams:
        for r in (None, False, True, 2, 4, -1, 1.0, 2**63, -(2**63)-1, 2**60):
            expected = observe(native, values, r)
            actual = observe(production, values, r)
            assert actual == expected, (name, values, r, actual, expected)
            cases += 1
    for n in range(7):
        for values in (list(range(n)), [x % 2 for x in range(n)]):
            for r in range(n + 2):
                assert list(production(values, r)) == list(native(values, r)), (name, values, r)
                cases += 1


keyword_events = []


class Keyword(str):
    __hash__ = str.__hash__

    def __eq__(self, other):
        keyword_events.append((str(self), str(other)))
        return str.__eq__(self, other)

    def __ne__(self, other):
        raise AssertionError('unexpected keyword __ne__')


bindings = (
    ((), {'iterable': [1], 'r': 1}),
    (([1],), {'r': 1}),
    (([1],), {'iterable': [2]}),
    ((), {'iterable': [1]}),
    ((), {'r': 1}),
    (([1], 1), {'r': 1}),
)
for name in ('combinations', 'combinations_with_replacement', 'permutations'):
    for args, kwargs in bindings:
        observations = []
        for module in (itertools, managed):
            options = {Keyword(key): value for key, value in kwargs.items()}
            keyword_events.clear()
            try:
                outcome = list(getattr(module, name)(*args, **options))
            except TypeError:
                outcome = TypeError
            observations.append((outcome, list(keyword_events)))
        assert observations[0] == observations[1], (name, args, kwargs, observations)
        cases += 1


class Payload:
    pass


class FreshPayload:
    def __init__(self, refs, fail=False):
        self.refs = refs
        self.done = False
        self.fail = fail

    def __iter__(self):
        return self

    def __next__(self):
        if self.done:
            if self.fail:
                raise RuntimeError('partial pool')
            raise StopIteration
        self.done = True
        value = Payload()
        self.refs.append(weakref.ref(value))
        return value


for module in (itertools, managed):
    for name in ('combinations', 'combinations_with_replacement', 'permutations'):
        factory = getattr(module, name)
        for r in (0, 1, 2):
            refs = []
            source = FreshPayload(refs)
            source_ref = weakref.ref(source)
            selected = factory(source, r)
            del source
            gc.collect()
            assert source_ref() is None
            assert refs[0]() is not None
            rows = list(selected)
            del rows
            gc.collect()
            assert refs[0]() is not None  # Pool survives even empty exhaustion.
            selected.__init__(42, -1, unknown=1)
            gc.collect()
            assert refs[0]() is not None
            del selected
            gc.collect()
            assert refs[0]() is None
        refs = []
        try:
            factory(FreshPayload(refs, fail=True), 0)
        except RuntimeError:
            pass
        else:
            raise AssertionError('missing source error')
        gc.collect()
        assert len(refs) == 1 and refs[0]() is None
        for r in (-1, 2**60):
            refs = []
            try:
                factory(FreshPayload(refs), r)
            except (ValueError, MemoryError):
                pass
            else:
                raise AssertionError('missing r validation error')
            gc.collect()
            assert len(refs) == 1 and refs[0]() is None
        refs = []
        selected = factory(FreshPayload(refs), 1)
        row = next(selected)
        del selected
        gc.collect()
        assert row[0] is refs[0]()
        del row
        gc.collect()
        assert refs[0]() is None


class Index:
    def __index__(self):
        return 1


for module in (itertools, managed):
    for name in ('combinations', 'combinations_with_replacement'):
        assert list(getattr(module, name)([1, 2], Index())) == [(1,), (2,)]
    try:
        module.permutations([1], Index())
    except TypeError:
        pass
    else:
        raise AssertionError('permutations must reject non-int index objects')

print(f'combinatorial: {cases} differential cases; shared assertions and lifetime checks passed on {sys.version.split()[0]}')
