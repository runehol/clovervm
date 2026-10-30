"""Compare native bytearray searches with CPython 3.14.5.

Run explicitly from any directory:
    python3 tests/host/test_bytearray_search.py [path/to/clovervm]
The default binary is build-debug/src/clovervm. General buffer and custom-index
inputs are excluded by agreement; native error wording is deliberately omitted.
"""
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TRACE = r"""
# Same trace runs on CPython and CloverVM; error wording intentionally excluded.
sources = (b'', b'a', b'ababa', b'aaaaaa', b'\x00\x01\xff\x00', b'abcabc')
needles = (b'', b'a', b'aa', b'aba', b'abc', b'longer than source',
           bytearray(), bytearray(b'aba'), bytearray(b'\x00'),
           0, 1, 97, 255, True, False, -1, 256, 10**100, None, 'bad')
bounds = (None, -10**100, -6, -1, 0, 1, 5, 6, 10**100, 'bad')
methods = ('find', 'rfind', 'index', 'rindex', 'count', 'startswith', 'endswith')
for source in sources:
    a = bytearray(source)
    for needle in needles:
        for start in bounds:
            for end in bounds:
                for name in methods:
                    try:
                        print(getattr(bytearray, name)(a, needle, start, end))
                    except TypeError:
                        print('TypeError')
                    except ValueError:
                        print('ValueError')
    for needle in ((), (b'',), (b'x', b'a'), (b'abc', None),
                   (b'', None), (None, b''), ((b'',),),
                   (bytearray(b'abc'), bytearray()), (a,), (0,)):
        for start in bounds:
            for end in bounds:
                for name in ('startswith', 'endswith'):
                    try:
                        print(getattr(bytearray, name)(a, needle, start, end))
                    except TypeError:
                        print('TypeError')
"""


def main():
    if sys.implementation.name != "cpython" or sys.version_info[:3] != (3, 14, 5):
        raise RuntimeError("reference tests require CPython 3.14.5")
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else (
        ROOT / "build-debug" / "src" / "clovervm")
    # Also run the ordinary shared regressions on the reference interpreter.
    runpy.run_path(str(ROOT / "tests/python/builtin_bytearray_search.py"))
    with tempfile.TemporaryDirectory(prefix="clover-bytearray-search-") as folder:
        source = Path(folder) / "trace.py"
        source.write_text(TRACE)
        reference = subprocess.run(
            [sys.executable, str(source)], cwd=ROOT, check=True,
            capture_output=True, text=True)
        actual = subprocess.run(
            [str(binary), str(source)], cwd=ROOT,
            capture_output=True, text=True)
        if actual.returncode:
            raise AssertionError(
                f"CloverVM exited with {actual.returncode}:\n"
                f"{actual.stdout}{actual.stderr}")
    expected_lines = reference.stdout.splitlines()
    actual_lines = actual.stdout.splitlines()
    if actual_lines != expected_lines:
        for index, (expected, result) in enumerate(zip(expected_lines, actual_lines)):
            if expected != result:
                raise AssertionError(
                    f"trace result {index}: CPython={expected!r}, CloverVM={result!r}")
        raise AssertionError(
            f"trace lengths differ: CPython={len(expected_lines)}, "
            f"CloverVM={len(actual_lines)}")
    print(f"CPython 3.14.5: {len(expected_lines)} matching search results")


if __name__ == "__main__":
    main()
