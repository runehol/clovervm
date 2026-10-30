"""Compare the production managed algorithms with native CPython 3.14.5.

Run with python3.14; no production helpers are replaced. Host operator.index
has a broader protocol than CloverVM's operator.index, so custom index inputs
are covered by the interpreter regressions instead. Huge-n allocation behavior
and exact refcount-dependent finalization timing are intentionally excluded.
"""

import gc
import importlib.util
import itertools
from pathlib import Path
import weakref


path = Path(__file__).resolve().parents[2] / "stdlib" / "itertools.py"
spec = importlib.util.spec_from_file_location("clover_itertools", path)
managed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(managed)


class Source:
    def __init__(self, values, events):
        self.values = iter(values)
        self.events = events

    def __iter__(self):
        self.events.append("iter")
        return self

    def __next__(self):
        self.events.append("next")
        value = next(self.values)
        if isinstance(value, BaseException):
            raise value
        return value


def observe(factory, values, *args, **kwargs):
    events = []
    selected = factory(Source(values, events), *args, **kwargs)
    outputs = []
    for _ in range(len(values) + 3):
        try:
            outputs.append(("value", next(selected)))
        except BaseException as error:
            outputs.append(("error", type(error), error.args))
    return outputs, events


def right(a, b):
    return b


def pair(a, b):
    return (a, b)


cases = 0
for length in range(4):
    for values in itertools.product((0, 1, None, ValueError("source"), StopIteration("pause")), repeat=length):
        for func in (None, right, pair, 42):
            for initial in (None, 0, False, ()):
                expected = observe(itertools.accumulate, values, func, initial=initial)
                actual = observe(managed.accumulate, values, func, initial=initial)
                assert actual == expected, (values, func, initial, actual, expected)
                cases += 1
        for size in (1, 2, 3):
            for strict in (False, True):
                expected = observe(itertools.batched, values, size, strict=strict)
                actual = observe(managed.batched, values, size, strict=strict)
                assert actual == expected, (values, size, strict, actual, expected)
                cases += 1


for module in (itertools, managed):
    source = Source([], [])
    source_ref = weakref.ref(source)

    def callback(a, b):
        return b

    callback_ref = weakref.ref(callback)
    selected = module.accumulate(source, callback)
    del source, callback
    assert next(selected, "done") == "done"
    gc.collect()
    assert source_ref() is not None
    assert callback_ref() is not None
    del selected
    gc.collect()
    assert source_ref() is None
    assert callback_ref() is None

    total = Source([], [])  # A weak-referenceable arbitrary accumulator value.
    total_ref = weakref.ref(total)
    selected = module.accumulate([], initial=total)
    del total
    assert next(selected) is total_ref()
    assert next(selected, "done") == "done"
    gc.collect()
    assert total_ref() is not None
    del selected
    gc.collect()
    assert total_ref() is None

    source = Source([1], [])
    source_ref = weakref.ref(source)
    selected = module.batched(source, 2)
    del source
    assert next(selected) == (1,)
    gc.collect()
    assert source_ref() is not None  # Short non-strict batch retains source.
    assert next(selected, "done") == "done"
    gc.collect()
    assert source_ref() is None  # Empty exhaustion releases it.

print(f"{cases} accumulate/batched differential cases and source retention passed")
