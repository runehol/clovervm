"""Compare string padding with CPython 3.14.10.

Run: python3.14 tests/host/test_string_padding.py [path/to/clovervm]
Custom __index__, storage-limit allocation failures and diagnostic wording are
excluded by agreement; the VM-specific limits have separate interpreter tests.
Independent generated cases; no CPython tests are copied.
"""
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TRACE = r"""
class S(str):
    def __len__(self):
        raise AssertionError
    def __getitem__(self, key):
        raise AssertionError
    def __str__(self):
        raise AssertionError

class I(int):
    def __index__(self):
        raise AssertionError

# Compare code points so existing repr escaping differences do not affect this
# padding oracle, including embedded NULs.
def emit_result(result, text):
    codes = []
    for index in range(len(result)):
        codes.append(ord(result[index]))
    print(codes, result.__class__ is str, result is text)

sources = ('', 'a', 'ab', 'abc', '+1', '-1', '\u22121', '\U0001f600',
           'e\u0301', 'a\x00b')
widths = (-10**100, -9223372036854775808, -2, 0, False, True,
          2, 3, 4, 5, 6, I(7), 9223372036854775808, None, '4', 4.0)
fills = (' ', '-', '\x00', '\xe9', '\U0001f600', S('x'), '', 'xx',
         'e\u0301', None, 1, b'x')
for original in sources:
    for text in (original, S(original)):
        for width in widths:
            for name in ('ljust', 'rjust', 'center'):
                method = getattr(str, name)
                for fill in fills:
                    try:
                        result = method(text, width, fill)
                        emit_result(result, text)
                    except TypeError:
                        print('TypeError')
                    except OverflowError:
                        print('OverflowError')
                try:
                    result = method(text, width)
                    emit_result(result, text)
                except TypeError:
                    print('TypeError')
                except OverflowError:
                    print('OverflowError')
            try:
                result = str.zfill(text, width)
                emit_result(result, text)
            except TypeError:
                print('TypeError')
            except OverflowError:
                print('OverflowError')
"""


def main():
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError('reference tests require CPython 3.14.10')
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else (
        ROOT / 'build-debug/src/clovervm')
    runpy.run_path(str(ROOT / 'tests/python/string_padding.py'))
    with tempfile.TemporaryDirectory(prefix='clover-string-padding-') as folder:
        source = Path(folder) / 'trace.py'
        source.write_text(TRACE, encoding='utf-8')
        reference = subprocess.run([sys.executable, str(source)], cwd=ROOT,
                                   check=True, capture_output=True, text=True)
        actual = subprocess.run([str(binary), str(source)], cwd=ROOT,
                                capture_output=True, text=True)
        if actual.returncode:
            raise AssertionError(
                f'CloverVM exited with {actual.returncode}:\n'
                f'{actual.stdout}{actual.stderr}')
    expected = reference.stdout.splitlines()
    observed = actual.stdout.splitlines()
    if expected != observed:
        for index, (wanted, got) in enumerate(zip(expected, observed)):
            if wanted != got:
                raise AssertionError(
                    f'trace result {index}: CPython={wanted!r}, CloverVM={got!r}')
        raise AssertionError(f'trace lengths: CPython={len(expected)}, VM={len(observed)}')
    print(f'CPython 3.14.10: {len(expected)} matching string-padding results')


if __name__ == '__main__':
    main()
