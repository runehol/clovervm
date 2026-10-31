"""Differential trace for the bounded bytearray splitting/partitioning APIs.

Run with CPython 3.14.10:
    python3.14 tests/host/test_bytearray_split.py [path/to/clovervm]
General buffers and custom __index__ inputs are intentionally excluded.
Native diagnostic wording is excluded; exception types/order are compared.
"""
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TRACE = r"""
sources = [b'', b'a', b'ababa', b'aaaaaa', b'a:b::c:', b'  a b  ',
           b' \t\r\n\v\f ', b'\x00a\xff\x00', b'\x1c\x85\xa0']
for first in (b'a', b':', b' '):
    for second in (b'a', b':', b' '):
        for third in (b'a', b':', b' '):
            sources.append(first + second + third)
separators = (None, b'', b':', b'a', b'aa', b'aba', b'longer than source',
              b'\x00', b'\xff', bytearray(), bytearray(b':'), bytearray(b'aba'),
              1, True, 'a', [], (b'a',))
limits = (-10**100, -9223372036854775809, -9223372036854775808,
          -2, -1, 0, 1, 2, 288230376151711744, 9223372036854775807,
          9223372036854775808, 10**100, True, False, None, '1', 1.0)
for data in sources:
    a = bytearray(data)
    for separator in separators:
        for limit in limits:
            for name in ('split', 'rsplit'):
                try:
                    print(getattr(bytearray, name)(a, separator, limit))
                except TypeError:
                    print('TypeError')
                except ValueError:
                    print('ValueError')
                except OverflowError:
                    print('OverflowError')
        for name in ('partition', 'rpartition'):
            try:
                print(getattr(bytearray, name)(a, separator))
            except TypeError:
                print('TypeError')
            except ValueError:
                print('ValueError')
    for name in ('split', 'rsplit', 'partition', 'rpartition'):
        try:
            print(getattr(bytearray, name)(a, a))
        except ValueError:
            print('ValueError')
"""


TRACE += r"""

def repeat_fixture(data, count):
    result = bytearray()
    block = bytearray(data)
    while count:
        if count % 2:
            result.extend(block)
        count = count // 2
        if count:
            block.extend(block)
    return bytes(result)


def random_fixture(count, seed):
    result = bytearray()
    for index in range(count):
        seed = (seed * 1664525 + 1013904223) % 4294967296
        result.append(seed % 256)
    return bytes(result)


def trace_long_separators():
    for width in (17, 32, 64, 129):
        data = random_fixture(width * 3 + 5, width + 101)
        for separator in (repeat_fixture(b'ab\x00\xff', width)[:width],
                          random_fixture(width, width + 1234),
                          repeat_fixture(b'a', width - 1) + b'b'):
            for payload in (b'', data, separator + data + separator,
                            separator + b'ba' + separator):
                source = bytearray(payload)
                for sep in (separator, bytearray(separator)):
                    for limit in (-1, 0, 1, 2):
                        print(source.split(sep, limit))
                        print(source.rsplit(sep, limit))
                    print(source.partition(sep))
                    print(source.rpartition(sep))


trace_long_separators()
"""


def main():
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError('reference tests require CPython 3.14.10')
    binary = (Path(sys.argv[1]).resolve() if len(sys.argv) > 1
              else ROOT / 'build-debug/src/clovervm')
    # Shared assertions also cover identity/mutation, keywords, and hook bypass.
    runpy.run_path(str(ROOT / 'tests/python/builtin_bytearray_split.py'))
    with tempfile.TemporaryDirectory(prefix='clover-bytearray-split-') as folder:
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
    if actual_lines != expected_lines:
        for index, (reference, result) in enumerate(zip(expected_lines, actual_lines)):
            if reference != result:
                raise AssertionError(
                    f'trace {index}: CPython={reference!r}, CloverVM={result!r}')
        raise AssertionError(f'trace lengths differ: CPython={len(expected_lines)}, '
                             f'CloverVM={len(actual_lines)}')
    print(f'CPython 3.14.10: {len(expected_lines)} matching split/partition results')


if __name__ == '__main__':
    main()
