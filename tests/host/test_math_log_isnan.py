"""CPython 3.14.10 and Decimal oracle for managed log/isnan.

Run: python3.14 tests/host/test_math_log_isnan.py [path/to/clovervm]
Custom conversion hooks and existing wrapper call conventions are excluded.
"""
from decimal import Decimal, localcontext
import importlib.util
import math
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError('reference tests require CPython 3.14.10')
    binary = (Path(sys.argv[1]).resolve() if len(sys.argv) > 1
              else ROOT / 'build-debug/src/clovervm')
    shared = ['test_math_log_integers.py', 'test_math_isnan_conversion.py']
    for name in shared:
        runpy.run_path(str(ROOT / 'tests/python' / name))

    # Exercise the actual managed source with CPython libm primitives as well
    # as the VM below. This adapter supplies no integer log workaround.
    spec = importlib.util.spec_from_file_location('managed_math', ROOT / 'stdlib/math.py')
    managed = importlib.util.module_from_spec(spec)
    previous = sys.modules.get('_math')
    sys.modules['_math'] = math
    try:
        spec.loader.exec_module(managed)
    finally:
        if previous is None:
            del sys.modules['_math']
        else:
            sys.modules['_math'] = previous
    sys.modules['math'] = managed
    try:
        for name in shared:
            runpy.run_path(str(ROOT / 'tests/python' / name))
    finally:
        sys.modules['math'] = math

    values = [1, 2, 10**20, 10**400, 10**1000]
    for boundary in [(1 << 1024) - (1 << 971),
                     (1 << 1024) - (1 << 970)]:
        values.extend([boundary - 1, boundary, boundary + 1])
    # Powers, overflow-boundary neighbors, halfway significands and sticky bits.
    for exponent in [53, 60, 100, 1023, 1024, 1025, 1330, 2048, 10000]:
        power = 1 << exponent
        half = 1 << (exponent - 53)
        for offset in [-1, 0, 1, half - 1, half, half + 1,
                       3 * half - 1, 3 * half, 3 * half + 1]:
            values.append(power + offset)
    # Independent varied mantissas, not just exact powers.
    seed = 12345
    for exponent in [64, 1024, 1400, 4096]:
        for _ in range(20):
            seed = (seed * 1103515245 + 12345) % (1 << 31)
            values.append(((1 << 60) + seed * seed) << (exponent - 60))

    source = ['import math']
    maximum_ulp = 0.0
    with localcontext() as ctx:
        ctx.prec = 100
        for index, value in enumerate(values):
            # Keep each generated code object's operator-cache table bounded.
            if index % 8 == 0:
                source.append(f'def fixtures_{index // 8}():')
            actual = managed.log(value)
            references = [math.log(value), float(Decimal(value).ln())]
            for expected in references:
                tolerance = 2 * math.ulp(expected)
                assert abs(actual - expected) <= tolerance, (value, actual, expected)
                maximum_ulp = max(maximum_ulp, abs(actual - expected) / math.ulp(expected))
                source.append(f'    assert abs(math.log({value}) - {expected!r}) <= {tolerance!r}')
            # Both numerator and base follow the repaired integer path.
            expected = math.log(value, 10**400)
            tolerance = 4 * math.ulp(expected)
            assert abs(managed.log(value, 10**400) - expected) <= tolerance
            source.append(f'    assert abs(math.log({value}, 10**400) - {expected!r}) <= {tolerance!r}')
            if index % 8 == 7 or index == len(values) - 1:
                source.append(f'fixtures_{index // 8}()')

    with tempfile.TemporaryDirectory(prefix='clover-math-oracle-') as folder:
        generated = Path(folder) / 'fixtures.py'
        generated.write_text('\n'.join(source) + '\n')
        for path in [*(ROOT / 'tests/python' / name for name in shared), generated]:
            result = subprocess.run([str(binary), str(path)], cwd=ROOT,
                                    capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f'{path.name}: VM exit {result.returncode}\n'
                                     f'{result.stdout[-2000:]}{result.stderr}')
    print(f'CPython 3.14.10/100-digit Decimal: {len(values)} log fixtures; '
          f'host maximum error {maximum_ulp} ULP; shared log/isnan tests passed')


if __name__ == '__main__':
    main()
