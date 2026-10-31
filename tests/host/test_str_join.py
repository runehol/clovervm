"""Compare str.join with CPython 3.14.10.

Run explicitly:
    python3.14 tests/host/test_str_join.py [path/to/clovervm]
Defaults to build-debug/src/clovervm. Length hints, sequence-only fallback,
and empty-result interning are excluded by agreement.
"""
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TRACE = r"""
# Deterministic result/error matrix, compatible with CloverVM's supported syntax.
class S(str):
    def __len__(self):
        raise AssertionError('length hook')
    def __getitem__(self, index):
        raise AssertionError('index hook')

def run_matrix():
    pieces = ['', 'a', '\x00\U0001f642\ud800', b'x', S('sub'), None, 7, ['text']]
    for separator in ('', '-', S('\x00')):
        for a in pieces:
            for b in pieces:
                for shape in range(3):
                    values = [a, b]
                    if shape == 1:
                        values = (a, b)
                    if shape == 2:
                        values = iter(values)
                    try:
                        result = separator.join(values)
                        points = []
                        for character in result:
                            points.append(ord(character))
                        print('ok', result.__class__.__name__, points)
                    except TypeError as error:
                        print('error', str(error))

run_matrix()
"""


def main():
    if sys.implementation.name != "cpython" or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError("reference tests require CPython 3.14.10")
    binary = (Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else
              ROOT / "build-debug/src/clovervm")
    runpy.run_path(str(ROOT / "tests/python/str_join.py"))
    with tempfile.TemporaryDirectory(prefix="clover-str-join-reference-") as folder:
        source = Path(folder) / "trace.py"
        source.write_text(TRACE)
        reference = subprocess.run(
            [sys.executable, str(source)], cwd=ROOT, check=True,
            capture_output=True, text=True)
        actual = subprocess.run(
            [str(binary), str(source)], cwd=ROOT, capture_output=True, text=True)
        if actual.returncode:
            raise AssertionError(
                f"CloverVM exited with {actual.returncode}:\n"
                f"{actual.stdout}{actual.stderr}")
    expected_lines = reference.stdout.splitlines()
    actual_lines = actual.stdout.splitlines()
    assert len(expected_lines) == 576
    if actual_lines != expected_lines:
        for index, (expected, result) in enumerate(zip(expected_lines, actual_lines)):
            if expected != result:
                raise AssertionError(
                    f"trace result {index}: CPython={expected!r}, CloverVM={result!r}")
        raise AssertionError(
            f"trace lengths differ: CPython={len(expected_lines)}, "
            f"CloverVM={len(actual_lines)}")
    print(f"CPython 3.14.10: {len(expected_lines)} matching str.join results")


if __name__ == "__main__":
    main()
