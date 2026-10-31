"""CPython 3.14.10 oracle for the bounded binary stripping APIs.

Run: python3.14 tests/host/test_binary_strip.py [path/to/clovervm]
General buffers and bytearray subclasses are outside this slice.
Native diagnostic wording differs; compare results and exception types.
"""
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TRACE = r'''
class B(bytes):
    def __bytes__(self):
        raise AssertionError
    def __len__(self):
        raise AssertionError
    def __getitem__(self, key):
        raise AssertionError
    def __iter__(self):
        raise AssertionError

sources = [b'', b'abc', b'aaa', b' \t\n\r\v\f ', b'abbaZbaab',
           b'\x00\xffZ\xff\x00', b'\x1c\x85\xa0']
alphabet = bytearray()
for value in range(256):
    one = bytearray()
    one.append(value)
    byte = bytes(one)
    sources.append(byte + b'Z' + byte)
    alphabet.append(value)
sources.append(bytes(alphabet))
for cls in (bytes, bytearray):
    for data in sources:
        source = cls(data)
        charsets = (None, b'', bytearray(), b'ab', b'baab', bytearray(b'ab'),
                    B(b'ab'), b'\x00\xff', alphabet, source,
                    0, True, 'a', [], (b'a',), 1.0)
        for name in ('strip', 'lstrip', 'rstrip'):
            method = getattr(cls, name)
            print(list(method(source)))
            for chars in charsets:
                try:
                    result = method(source, chars)
                    print(list(result), result.__class__ is cls, result is source)
                except TypeError:
                    print('TypeError')
for name in ('strip', 'lstrip', 'rstrip'):
    for data in (b'', b'abc', b' ab ', b'abZba'):
        for chars in (None, b'', B(b'ab'), bytearray(b'ab')):
            result = getattr(bytes, name)(B(data), chars)
            print(list(result), result.__class__ is bytes)
'''


def main():
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError('reference tests require CPython 3.14.10')
    binary = (Path(sys.argv[1]).resolve() if len(sys.argv) > 1
              else ROOT / 'build-debug/src/clovervm')
    runpy.run_path(str(ROOT / 'tests/python/binary_strip.py'))
    with tempfile.TemporaryDirectory(prefix='clover-binary-strip-') as folder:
        source = Path(folder) / 'trace.py'
        source.write_text(TRACE)
        expected = subprocess.run([sys.executable, str(source)], cwd=ROOT,
                                  check=True, capture_output=True, text=True)
        actual = subprocess.run([str(binary), str(source)], cwd=ROOT,
                                capture_output=True, text=True)
        if actual.returncode:
            raise AssertionError(f'CloverVM exited with {actual.returncode}:\n'
                                 f'{actual.stdout[-1000:]}{actual.stderr}')
    expected_lines = expected.stdout.splitlines()
    actual_lines = actual.stdout.splitlines()
    if expected_lines != actual_lines:
        for index, (reference, result) in enumerate(zip(expected_lines, actual_lines)):
            if reference != result:
                raise AssertionError(f'trace {index}: CPython={reference!r}, '
                                     f'CloverVM={result!r}')
        raise AssertionError(f'trace lengths differ: CPython={len(expected_lines)}, '
                             f'CloverVM={len(actual_lines)}')
    print(f'CPython 3.14.10: {len(expected_lines)} matching binary strip results')


if __name__ == '__main__':
    main()
