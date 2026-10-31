"""Check production quantiles against CPython 3.14.10 and exact rational ranks.

Run with python3.14. The rational oracle applies only to integer samples;
nonfinite/overflow-prone float arithmetic has separate explicit shared fixtures.
This does not claim universal numerical accuracy or NaN sort equivalence.
"""

from fractions import Fraction
import gc
import importlib.util
import math
from pathlib import Path
import random
import statistics
import struct
import sys
import weakref


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('clover_statistics', ROOT / 'stdlib/statistics.py')
managed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(managed)
for module in (statistics, managed):
    previous = sys.modules['statistics']
    sys.modules['statistics'] = module
    try:
        for name in ('test_statistics_quantiles.py', 'test_statistics_quantiles_callbacks.py'):
            path = ROOT / 'tests/python' / name
            exec(compile(path.read_text(), str(path), 'exec'), {})
    finally:
        sys.modules['statistics'] = previous


def rational_cuts(data, n, method):
    ordered = sorted(data)
    if len(ordered) == 1:
        return ordered * (n - 1)
    result = []
    for i in range(1, n):
        # Rank is measured from zero; exclusive ranks can lie outside the sample.
        rank = (Fraction(i * (len(ordered) - 1), n) if method == 'inclusive'
                else Fraction(i * (len(ordered) + 1), n) - 1)
        left = max(0, min(math.floor(rank), len(ordered) - 2))
        value = ordered[left] + (rank - left) * (ordered[left + 1] - ordered[left])
        result.append(float(value))
    return result


def observe(function, *args, **kwargs):
    try:
        result = function(*args, **kwargs)
    except (ValueError, TypeError, OverflowError) as error:
        return ('error', type(error).__name__)
    return ('ok', [struct.pack('>d', value) for value in result])


rng = random.Random(31410)
count = 0
for size in range(2, 24):
    for exponent in (0, 53, 200, 1023, 1329):
        data = [rng.randrange(-1024, 1025) * 2**exponent for _ in range(size)]
        for n in (1, 2, 3, 4, 7, 16, 31):
            for method in ('exclusive', 'inclusive'):
                expected = observe(rational_cuts, data, n, method)
                assert observe(statistics.quantiles, data, n=n, method=method) == expected
                assert observe(managed.quantiles, data, n=n, method=method) == expected
                count += 1
# Validation does not eagerly coerce n; invalid method can win over invalid n.
for data in ([], [7], [0, 10]):
    for n in (0, False, True, 1, 4, 1.0, 1.5, math.nan, math.inf):
        for method in ('exclusive', 'inclusive', 'bad', None):
            assert observe(managed.quantiles, data, n=n, method=method) == observe(
                statistics.quantiles, data, n=n, method=method)
            count += 1


# Independent lifetime checks. CPython refcount timing and VM reclamation timing
# need not match, but a returned singleton list must own every repeated reference.
class Token:
    pass


for module in (statistics, managed):
    token = Token()
    ref = weakref.ref(token)
    result = module.quantiles(iter([token]))
    del token
    gc.collect()
    assert ref() is result[0] and result[2] is result[0]
    del result
    gc.collect()
    assert ref() is None

    refs = []

    class Number:
        def __init__(self, value):
            self.value = value
            refs.append(weakref.ref(self))

        def __lt__(self, other):
            gc.collect()
            assert all(ref() is not None for ref in refs)
            return self.value < other.value

    # n=1 still consumes and sorts, then releases the temporary sorted copy.
    assert module.quantiles((Number(i) for i in (3, 1, 2)), n=1) == []
    gc.collect()
    assert all(ref() is None for ref in refs)

print(f'{count} quantiles differential/rational cases and shared/lifetime regressions passed')

# Optional end-to-end VM differential, using the same matrix source in both VMs.
# The binary must be built from this checkout so imports use production sources.
if len(sys.argv) == 2:
    import subprocess

    matrix = '''
import math
import statistics as s
for size in range(1, 18):
    for scale in (1, 2**53, 10**100):
        data = []
        for i in range(size):
            data.append(((i * 17 + size) % 29 - 14) * scale)
        for n in (1, 2, 4, 9, 21):
            for method in ('inclusive', 'exclusive'):
                print(repr(s.quantiles(data, n=n, method=method)))
for data in ([-0.0, -0.0], [5e-324, 5e-324], [1e308, 1e308],
             [float('inf'), float('inf')], [-float('inf'), float('inf')]):
    for method in ('inclusive', 'exclusive'):
        print(repr(s.quantiles(data, method=method)))
'''
    expected = subprocess.run([sys.executable, '-c', matrix], check=True,
                              capture_output=True, text=True).stdout
    actual = subprocess.run([sys.argv[1], '-c', matrix], check=True,
                            capture_output=True, text=True).stdout
    assert actual == expected, 'end-to-end quantiles differential failed'
    print(f'{len(actual.splitlines())} end-to-end VM differential cases passed')
