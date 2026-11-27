"""Check CloverVM statistics against CPython 3.14.10 and exact rational oracles.

Run: python3.14 tests/host/test_statistics_spread.py [build-debug/src/clovervm]
"""
from fractions import Fraction
import gc
import importlib.util
import math
from pathlib import Path
import random
import runpy
import statistics
import struct
import subprocess
import sys
import tempfile
import weakref


ROOT = Path(__file__).resolve().parents[2]
SHARED = ROOT / "tests/python/test_statistics_spread.py"
PAYLOADS = ROOT / "tests/python/test_statistics_spread_payloads.py"
MAX_FINITE_BITS = 0x7FEFFFFFFFFFFFFF


def load_managed(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def managed_statistics():
    # Supply host libm functions to CloverVM's managed math module, so the
    # kernel exercises the production math.isqrt as well as statistics code.
    sys.modules["_math"] = math
    try:
        managed_math = load_managed("clover_math", ROOT / "stdlib/math.py")
    finally:
        del sys.modules["_math"]
    sys.modules["math"] = managed_math
    try:
        return load_managed("clover_statistics", ROOT / "stdlib/statistics.py")
    finally:
        sys.modules["math"] = math


def as_float(bits):
    return struct.unpack(">d", bits.to_bytes(8, "big"))[0]


def rounded_sqrt(target):
    # Positive binary64 bit patterns are ordered by value. Find the largest
    # float whose exact square is <= target, then compare the midpoint square.
    low, high = 0, MAX_FINITE_BITS
    while low < high:
        mid = (low + high + 1) // 2
        if Fraction(as_float(mid)) ** 2 <= target:
            low = mid
        else:
            high = mid - 1
    lower = Fraction(as_float(low))
    upper = (Fraction(as_float(low + 1)) if low < MAX_FINITE_BITS
             else Fraction(2**1024))
    midpoint_squared = ((lower + upper) / 2) ** 2
    if target > midpoint_squared or (target == midpoint_squared and low % 2):
        low += 1
    result = as_float(low)
    if math.isinf(result):
        raise OverflowError
    return result


def expected_spread(data, center, correction, root):
    if center is None:
        values = list(map(Fraction, data))
        mean = sum(values) / len(values)
        terms = [(value - mean) ** 2 for value in values]
        integer_terms = all(isinstance(value, int) for value in data)
    else:
        deltas = [value - center for value in data]
        squares = [delta * delta for delta in deltas]
        terms = list(map(Fraction, squares))
        integer_terms = all(isinstance(square, int) for square in squares)
    ratio = sum(terms) / (len(data) - correction)
    if root:
        return rounded_sqrt(ratio)
    if integer_terms and ratio.denominator == 1:
        return ratio.numerator
    return float(ratio)


def cases():
    min_normal = math.ldexp(1.0, -1022)
    max_float = sys.float_info.max
    offset = 10**1000
    cases = [
        ([0.0, 5e-324], None),
        ([0.0, 5e-324], 0.0),
        ([0.0, 1.5e-323], None),
        ([0.0, 2.5e-323], None),
        ([0.0, min_normal * 2], None),
        ([0.0, math.nextafter(min_normal * 2, 0.0)], None),
        ([min_normal, math.nextafter(min_normal, 0.0)], None),
        ([offset, offset + 2], None),
        ([offset, offset + 2], offset),
        ([0, 10**200], None),
        ([0, 2**1025 - 2**971 - 2], None),
        ([0, 2**1025 - 2**971], None),
        ([0.0, max_float], None),
        ([-max_float, max_float], None),
        ([1.0, math.nextafter(1.0, math.inf), math.nextafter(1.0, 0.0)], None),
        ([10**100 + 7, 1.25, 5e-324, -1.25], None),
        ([-1.25, 5e-324, 1.25, 10**100 + 7], None),
        ([int(1e308) + 2, 1e308], None),
        ([0, (1 << 54) + 2], None),
        ([0, (1 << 54) + 6], None),
        ([math.ldexp(1.0, -537), 1.25, 1], 0),
        ([True, True], False),
        ([True, False], 0.0),
    ]
    rng = random.Random(8097)
    for index in range(80):
        base = math.ldexp(1.0, rng.randint(-1070, 480))
        values = [base, math.nextafter(base, math.inf),
                  math.nextafter(base, 0.0),
                  math.ldexp(rng.random(), rng.randint(-1070, 480))]
        cases.append((values, base if index % 2 else None))
    return cases


def result_or_overflow(function, *args):
    try:
        return function(*args)
    except OverflowError:
        return OverflowError


def compare(managed):
    functions = (("pvariance", 0, False), ("variance", 1, False),
                 ("pstdev", 0, True), ("stdev", 1, True))
    checks = []
    for data, center in cases():
        for name, correction, root in functions:
            expected = result_or_overflow(expected_spread, data, center,
                                          correction, root)
            for module in (statistics, managed):
                actual = result_or_overflow(getattr(module, name), data, center)
                assert actual == expected and type(actual) is type(expected), (
                    module.__name__, name, data, center, actual, expected)
            call = f"s.{name}({data!r}, {center!r})"
            if expected is OverflowError:
                checks.append(f"try:\n    {call}\nexcept OverflowError:\n"
                              "    pass\nelse:\n    assert False\n")
            else:
                checks.append(f"check({call}, {expected!r}, {type(expected).__name__})\n")
    return checks


def check_retention(managed):
    class Token(float):
        pass

    for center in (None, 0.0):
        refs = []

        class Source:
            def __iter__(self):
                return self

            def __next__(self):
                gc.collect()
                assert all(ref() is None for ref in refs[:-1])
                if len(refs) == 12:
                    raise StopIteration
                token = Token(1)
                refs.append(weakref.ref(token))
                return token

        assert managed.pvariance(Source(), center) == (0.0 if center is None else 1.0)
        gc.collect()
        assert all(ref() is None for ref in refs)


def check_in_vm(binary, checks):
    with tempfile.TemporaryDirectory(prefix="clover-spread-oracle-") as folder:
        paths = [SHARED, PAYLOADS]
        # Keep each generated module well within the VM's constant table.
        for start in range(0, len(checks), 24):
            path = Path(folder) / f"fixtures_{start}.py"
            path.write_text("import statistics as s\n"
                            "def check(actual, expected, cls):\n"
                            "    assert actual == expected\n"
                            "    assert actual.__class__ is cls\n"
                            + "".join(checks[start:start + 24]))
            paths.append(path)
        for path in paths:
            result = subprocess.run([binary, str(path)], cwd=ROOT,
                                    capture_output=True, text=True)
            assert result.returncode == 0, (path.name, result.stdout, result.stderr)


def main():
    if sys.implementation.name != "cpython" or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError("reference requires CPython 3.14.10")
    managed = managed_statistics()
    runpy.run_path(str(SHARED))
    sys.modules["statistics"] = managed
    try:
        runpy.run_path(str(SHARED))
        runpy.run_path(str(PAYLOADS))
    finally:
        sys.modules["statistics"] = statistics
    check_retention(managed)
    checks = compare(managed)
    if len(sys.argv) == 2:
        check_in_vm(str(Path(sys.argv[1]).resolve()), checks)
    print(f"{len(checks)} exact-rational and CPython comparisons passed")


if __name__ == "__main__":
    main()
