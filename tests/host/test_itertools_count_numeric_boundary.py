"""Check production count's numeric policy on host CPython 3.14.5.

The VM currently lacks mixed heap-int/float addition. This harness supplies no
arithmetic replacements: it loads the actual public module on the host and
retains the CPython reference expectations for that deferred runtime boundary.
"""

import importlib.util
import itertools
from pathlib import Path


path = Path(__file__).resolve().parents[2] / "stdlib" / "itertools.py"
spec = importlib.util.spec_from_file_location("clover_itertools", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

for factory in (itertools.count, module.count):
    for start, step in ((1.0, 2**60), (2**60, 1.0)):
        values = factory(start, step)
        current = start
        for _ in range(3):
            following = current + step
            actual = next(values)
            assert type(actual) is type(current)
            assert actual == current
            current = following
    for start, step in ((1.0, 10**400), (10**400, 1.0)):
        values = factory(start, step)
        for _ in range(2):
            try:
                next(values)
            except OverflowError:
                pass
            else:
                raise AssertionError("addition must overflow before returning")
