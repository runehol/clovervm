"""CPython 3.14.10 differential and independent Decimal geometric-mean oracle.

Run: python3.14 tests/host/test_statistics_geometric_mean.py [build-debug/src/clovervm]
Explicit per-fixture ULP budgets include inherited log/exp amplification; they
are regression limits, not a universal accuracy claim. No custom coercion hooks.
"""
from decimal import Decimal, localcontext
import gc
import importlib.util
import math
from pathlib import Path
import runpy
import statistics
import subprocess
import sys
import tempfile
import weakref

ROOT = Path(__file__).resolve().parents[2]
SHARED = ('test_statistics_geometric_mean.py',
          'test_statistics_geometric_callbacks.py')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError('reference requires CPython 3.14.10')
    # Use the production managed math algorithms with host libm primitives.
    previous = sys.modules.get('_math')
    sys.modules['_math'] = math
    try:
        managed_math = load('managed_math', ROOT / 'stdlib/math.py')
    finally:
        if previous is None:
            del sys.modules['_math']
        else:
            sys.modules['_math'] = previous
    sys.modules['math'] = managed_math
    try:
        managed = load('managed_statistics', ROOT / 'stdlib/statistics.py')
    finally:
        sys.modules['math'] = math

    for module in (statistics, managed):
        sys.modules['statistics'] = module
        try:
            for name in SHARED:
                runpy.run_path(str(ROOT / 'tests/python' / name))
        finally:
            sys.modules['statistics'] = statistics

        refs = []

        class Token(float):
            pass

        class Source:
            def __init__(self):
                self.index = 0

            def __iter__(self):
                return self

            def __next__(self):
                gc.collect()
                # The suspended producer retains just the previous input
                # during acquisition; no growing list of inputs is retained.
                if refs:
                    assert refs[-1]() is not None
                    assert all(ref() is None for ref in refs[:-1])
                if self.index == 12:
                    raise StopIteration
                self.index += 1
                token = Token(1)
                refs.append(weakref.ref(token))
                return token

        assert module.geometric_mean(Source()) == 1.0
        gc.collect()
        assert all(ref() is None for ref in refs)

    fixtures = [([4, 9], 1), ([54, 24, 36], 2), ([17.625], 2),
                ([2.0**1000, math.e, 2.0**-1000], 1),
                ([2.0**1000, 2.0**-1000], 1), ([5e-324], 1),
                ([1e308, 1e308], 1024), ([1e-300, 1e-300], 1024),
                ([sys.float_info.max], 1024),
                ([math.nextafter(sys.float_info.max, 0.0), sys.float_info.max], 1024),
                ([5e-324, 1e-323], 1), ([1e-323, 1.5e-323], 1),
                ([10**400, 1], 256), ([10**1000, 1, 1, 1], 1024)]
    for size in (2, 3, 5, 13, 31):
        fixtures.append(([(i * 17 + size) % 29 + 1 for i in range(size)], 8))
        for exponent in (-1000, -500, 500, 1000):
            fixtures.append(([(1 + (i % 7) / 8) * 2.0**exponent
                              for i in range(size)], 1024))

    checks = ['import math', 'import statistics as s']
    with localcontext() as ctx:
        ctx.prec = 160
        for index, (data, budget) in enumerate(fixtures):
            # Decimal(float) preserves the exact binary64 input. Independent
            # high-precision product/root arithmetic does not reuse binary logs.
            product = Decimal(1)
            for x in data:
                product *= Decimal(x)
            expected = float((product.ln() / Decimal(len(data))).exp())
            tolerance = budget * math.ulp(expected)
            for function in (statistics.geometric_mean, managed.geometric_mean):
                actual = function(data)
                assert abs(actual - expected) <= tolerance, (index, actual, expected, budget)
            # Check actual production VM code against both independent and
            # CPython references. Explicit budgets also allow platform libm.
            reference = statistics.geometric_mean(data)
            checks.append(f'def fixture_{index}():')
            checks.append(f'    actual = s.geometric_mean({data!r})')
            checks.append(f'    assert abs(actual - {expected!r}) <= {tolerance!r}')
            checks.append(f'    assert abs(actual - {reference!r}) <= {2 * tolerance!r}')
            checks.append(f'fixture_{index}()')

    if len(sys.argv) == 2:
        binary = str(Path(sys.argv[1]).resolve())
        with tempfile.TemporaryDirectory(prefix='clover-geomean-oracle-') as folder:
            generated = Path(folder) / 'fixtures.py'
            generated.write_text('\n'.join(checks) + '\n')
            for path in [*(ROOT / 'tests/python' / name for name in SHARED), generated]:
                result = subprocess.run([binary, str(path)], cwd=ROOT,
                                        capture_output=True, text=True)
                assert result.returncode == 0, (path.name, result.stdout, result.stderr)
    print(f'{len(fixtures)} Decimal/CPython fixtures; shared callback/lifetime checks passed')


if __name__ == '__main__':
    main()
