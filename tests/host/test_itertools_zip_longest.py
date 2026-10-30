"""Compare production managed zip_longest with native CPython 3.14.5.

Run with python3.14. The shared interpreter assertions run against both modules;
only the agreed sticky-exhaustion cases skip the unsafe native implementation.
Tuple recycling, refcount-sensitive output retention and allocation failure
ordering are intentionally excluded. No production helpers are replaced.
"""

import gc
import importlib.util
import itertools
from pathlib import Path
import sys
import weakref


root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("clover_itertools", root / "stdlib/itertools.py")
managed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(managed)
shared = compile((root / "tests/python/itertools_zip_longest.py").read_text(),
                 "tests/python/itertools_zip_longest.py", "exec")
exec(shared, {})
sys.modules["itertools"] = managed
try:
    exec(shared, {})
finally:
    sys.modules["itertools"] = itertools


class Source:
    def __init__(self, name, tokens, events):
        self.name = name
        self.tokens = iter(tokens)
        self.events = events

    def __iter__(self):
        self.events.append(("iter", self.name))
        return self

    def __next__(self):
        self.events.append(("next", self.name))
        token = next(self.tokens)
        if token == "stop":
            raise StopIteration("pause")
        if token == "error":
            raise ValueError("source")
        return token


def observe(factory, streams, shared=False):
    events = []
    sources = [Source(i, stream, events) for i, stream in enumerate(streams)]
    if shared:
        sources = [sources[0]] * 3
    selected = factory(*sources, fillvalue="F")
    outputs = []
    for _ in range(8):
        try:
            row = next(selected)
            assert type(row) is tuple
            outputs.append(("row", row))
        except BaseException as error:
            outputs.append(("error", type(error), error.args))
    # Observe remaining source contents too: closure must not consume them.
    remaining = []
    for source in sources:
        tail = []
        for _ in range(4):
            try:
                tail.append(("item", next(source)))
            except BaseException as error:
                tail.append(("error", type(error), error.args))
        remaining.append(tail)
    return outputs, events, remaining


streams = [()]
for length in (1, 2):
    streams.extend(itertools.product((0, None, "stop", "error"), repeat=length))
cases = 0
for count in range(4):
    for inputs in itertools.product(streams, repeat=count):
        expected = observe(itertools.zip_longest, inputs)
        actual = observe(managed.zip_longest, inputs)
        assert actual == expected, (inputs, actual, expected)
        cases += 1
for stream in streams:
    inputs = (stream,)
    assert observe(managed.zip_longest, inputs, True) == observe(itertools.zip_longest, inputs, True)
    cases += 1


class Payload:
    pass


class FailingAcquisition:
    def __iter__(self):
        raise ValueError("construction")


class FreshIterator:
    def __init__(self, refs):
        self.refs = refs

    def __iter__(self):
        source = Source("fresh", (), [])
        self.refs.append(weakref.ref(source))
        return source


for module in (itertools, managed):
    short = Source("short", (), [])
    final = Source("final", (1,), [])
    fill = Payload()
    refs = [weakref.ref(value) for value in (short, final, fill)]
    selected = module.zip_longest(short, final, fillvalue=fill)
    del short, final, fill
    row = next(selected)
    assert row[0] is refs[2]()
    del row
    gc.collect()
    assert refs[0]() is None  # Ordinary exhausted slots release their source.
    assert refs[1]() is not None
    assert refs[2]() is not None
    assert next(selected, "done") == "done"
    gc.collect()
    assert refs[1]() is not None  # The final source survives terminal closure.
    assert refs[2]() is not None
    del selected
    gc.collect()
    assert all(ref() is None for ref in refs)

    first = Source("first", ("error", 7), [])
    later = Source("later", (8,), [])
    refs = [weakref.ref(first), weakref.ref(later)]
    selected = module.zip_longest(first, later)
    del first, later
    try:
        next(selected)
    except ValueError:
        pass
    else:
        raise AssertionError("missing source error")
    gc.collect()
    assert all(ref() is not None for ref in refs)
    assert next(selected, "done") == "done"
    del selected
    gc.collect()
    assert all(ref() is None for ref in refs)

    refs = []
    try:
        module.zip_longest(FreshIterator(refs), FailingAcquisition())
    except ValueError:
        pass
    else:
        raise AssertionError("missing acquisition error")
    gc.collect()
    assert len(refs) == 1 and refs[0]() is None

    item = Payload()
    item_ref = weakref.ref(item)
    selected = module.zip_longest([item], [1, 2])
    row = next(selected)
    del item
    assert next(selected) == (None, 2)
    gc.collect()
    assert row[0] is item_ref()  # Returned tuples retain their own elements.
    del selected, row
    gc.collect()
    assert item_ref() is None

print(f"{cases} zip_longest differential cases, shared assertions and lifetime checks passed")
