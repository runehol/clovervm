"""CPython 3.14.10 differential coverage for bytes/bytearray ASCII casing.

Run: python3.14 tests/host/test_binary_case.py [path/to/clovervm]
Original tests. No general buffers or bytearray subclass construction.
"""
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TRACE = r"""
names = ('lower', 'upper', 'swapcase', 'capitalize', 'title')
data = bytearray()
for value in range(256):
    data.append(value)
sources = [b'', bytes(data), b"hELLo 12wORLD-isN'T\x00a\xffBC\x80dE"]
for value in range(256):
    one = bytearray()
    one.append(value)
    sources.append(bytes(one))
    sources.append(b'a' + bytes(one) + b'B')
    sources.append(bytes(one) + b'AB')
# Deterministic mixed contexts, independent of the implementation's algorithm.
seed = 197
for length in (2, 3, 17, 64, 257, 4097):
    source = bytearray()
    for index in range(length):
        seed = (seed * 1664525 + 1013904223) % 4294967296
        source.append((seed // 65536) % 256)
    sources.append(bytes(source))
for source in sources:
    for cls in (bytes, bytearray):
        value = cls(source)
        for name in names:
            print(list(getattr(cls, name)(value)))
"""


def main():
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError('reference tests require CPython 3.14.10')
    binary = (Path(sys.argv[1]).resolve() if len(sys.argv) > 1
              else ROOT / 'build-debug/src/clovervm')
    runpy.run_path(str(ROOT / 'tests/python/binary_case.py'))
    with tempfile.TemporaryDirectory(prefix='clover-case-differential-') as folder:
        source = Path(folder) / 'trace.py'
        source.write_text(TRACE)
        expected = subprocess.run([sys.executable, str(source)], cwd=ROOT,
                                  check=True, capture_output=True, text=True)
        actual = subprocess.run([str(binary), str(source)], cwd=ROOT,
                                check=True, capture_output=True, text=True)
    expected_lines = expected.stdout.splitlines()
    actual_lines = actual.stdout.splitlines()
    if actual_lines != expected_lines:
        for index, (reference, result) in enumerate(zip(expected_lines, actual_lines)):
            if reference != result:
                raise AssertionError(f'case {index}: {reference!r} != {result!r}')
        raise AssertionError(f'line counts: {len(expected_lines)} != {len(actual_lines)}')
    print(f'PASS: {len(expected_lines)} differential results')


if __name__ == '__main__':
    main()
