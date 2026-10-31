"""Compare binary padding with CPython 3.14.10.

Run: python3.14 tests/host/test_binary_padding.py [path/to/clovervm]
Independent generated cases; no CPython test code copied. General buffers,
custom __index__, bytearray subclasses, storage-limit allocation failures and
exact diagnostic wording are excluded; VM-only limits have separate tests.
"""
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TRACE = r"""
class B(bytes):
    def __len__(self):
        raise AssertionError
    def __getitem__(self, key):
        raise AssertionError
    def __bytes__(self):
        raise AssertionError

class I(int):
    def __index__(self):
        raise AssertionError
    def __int__(self):
        raise AssertionError

def emit(result, source, cls):
    print(list(result), result.__class__ is cls, result is source)

sources = (b'', b'a', b'ab', b'abc', b'+1', b'-1', b'+-1', b'-',
           b'\x00\xff', b'\xe2\x88\x921')
widths = (-10**100, -9223372036854775808, I(-2**60), -2, 0, False, True,
          2, 3, 4, 5, 6, I(7), 9223372036854775808, None, '4', 4.0)
fills = (b' ', b'-', b'\x00', b'\xff', B(b'x'), bytearray(b'\x80'), b'',
         b'xx', B(b''), bytearray(b'xx'), None, 1, 'x', [])
for original in sources:
    for source in (original, B(original), bytearray(original)):
        cls = bytearray if source.__class__ is bytearray else bytes
        for width in widths:
            for name in ('ljust', 'rjust', 'center'):
                method = getattr(cls, name)
                for fill in fills:
                    try:
                        emit(method(source, width, fill), source, cls)
                    except TypeError:
                        print('TypeError')
                    except OverflowError:
                        print('OverflowError')
                try:
                    emit(method(source, width), source, cls)
                except TypeError:
                    print('TypeError')
                except OverflowError:
                    print('OverflowError')
            try:
                emit(cls.zfill(source, width), source, cls)
            except TypeError:
                print('TypeError')
            except OverflowError:
                print('OverflowError')
for byte in range(256):
    fill = bytes([byte])
    for cls in (bytes, bytearray):
        for name in ('ljust', 'rjust', 'center'):
            emit(getattr(cls, name)(cls(b'\x00\xff'), 7, fill), None, cls)
"""


def main():
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError('reference tests require CPython 3.14.10')
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else (
        ROOT / 'build-debug/src/clovervm')
    runpy.run_path(str(ROOT / 'tests/python/binary_padding.py'))
    with tempfile.TemporaryDirectory(prefix='clover-binary-padding-') as folder:
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
    print(f'CPython 3.14.10: {len(expected)} matching binary-padding results')


if __name__ == '__main__':
    main()
