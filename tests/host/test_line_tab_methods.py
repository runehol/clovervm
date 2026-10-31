"""Independent differential oracle for line/tab methods; CPython 3.14.10.

Run: python3.14 tests/host/test_line_tab_methods.py build-debug/src/clovervm
Custom non-int __index__, allocator exhaustion, diagnostic wording and incidental
empty/single-character interning identities are outside this comparison. Flags
use the supported truth subset; existing NotImplemented truth and non-integer
length-result coercion deviations are unchanged.
"""
from pathlib import Path
import itertools
import random
import runpy
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def make_traces():
    rng = random.Random(31026)
    breaks = '\n\r\v\f\x1c\x1d\x1e\x85\u2028\u2029'
    strings = ['', 'ab', 'ab\n', '\U0001f600\tX', 'e\u0301\t']
    strings += ['a' + a + b + 'b' for a, b in itertools.product(breaks, repeat=2)]
    strings += [''.join(rng.choice('ab\t\x00\U0001f600' + breaks)
                        for _ in range(rng.randrange(25))) for _ in range(180)]
    binary = [b'', b'ab', b'ab\n']
    binary += [bytes([97, x, 9, 98]) for x in range(256)]
    binary += [bytes(rng.choice([0, 9, 10, 11, 12, 13, 28, 133, 255, 97])
                     for _ in range(rng.randrange(25))) for _ in range(180)]
    prefix = '''
class S(str): pass
class B(bytes): pass
class I(int):
    def __index__(self):
        raise AssertionError('stored integer override')
def codes(value):
    if isinstance(value, str):
        result = []
        for ch in value:
            result.append(ord(ch))
        return result
    return list(value)
def check(cls, sub, data):
    for raw in data:
        for text in (cls(raw), sub(raw)):
            for flag in (False, True, None, 0, 1, [], [1]):
                parts = text.splitlines(keepends=flag)
                contents = []
                types = []
                identities = []
                for part in parts:
                    contents.append(codes(part))
                    types.append(part.__class__ is cls)
                    if len(text) > 1:
                        identities.append(part is text)
                print(contents, types, identities)
            for size in (-2147483648, -1, 0, False, True, 2, 4, 8, I(9)):
                out = text.expandtabs(tabsize=size)
                print(codes(out), out.__class__ is cls,
                      out is text if len(text) > 1 else False)
            for bad in (2147483648, -2147483649, 10**100, -10**100, None, 1.5):
                try:
                    text.expandtabs(bad)
                except OverflowError:
                    print('OverflowError')
                except TypeError:
                    print('TypeError')
                else:
                    raise AssertionError('expected conversion error')
'''
    # Keep each module below CloverVM's existing constant-table limit.
    for cls, sub, cases in (('str', 'S', strings), ('bytes', 'B', binary),
                            ('bytearray', 'bytearray', binary)):
        for start in range(0, len(cases), 32):
            yield prefix + f'check({cls}, {sub}, {ascii(cases[start:start + 32])})\n'



def main():
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 10):
        raise RuntimeError('requires CPython 3.14.10')
    binary = Path(sys.argv[1]).resolve()
    runpy.run_path(str(ROOT / 'tests/python/line_tab_methods.py'))
    wanted, got = [], []
    with tempfile.TemporaryDirectory(prefix='clover-line-tab-oracle-') as folder:
        path = Path(folder) / 'trace.py'
        for index, source in enumerate(make_traces()):
            path.write_text(source, encoding='utf-8')
            expected = subprocess.run([sys.executable, str(path)], cwd=ROOT,
                                      capture_output=True, text=True, check=True)
            actual = subprocess.run([str(binary), str(path)], cwd=ROOT,
                                    capture_output=True, text=True)
            if actual.returncode:
                raise AssertionError(f'VM batch {index} exit {actual.returncode}: '
                                     f'{actual.stdout[-1500:]}\n{actual.stderr}')
            wanted.extend(expected.stdout.splitlines())
            got.extend(actual.stdout.splitlines())
    if wanted != got:
        for index, (a, b) in enumerate(zip(wanted, got)):
            if a != b:
                raise AssertionError(f'trace {index}: CPython={a}, VM={b}')
        raise AssertionError(f'trace lengths: CPython={len(wanted)}, VM={len(got)}')
    print(f'CPython 3.14.10: {len(wanted)} matching line/tab results')


if __name__ == '__main__':
    main()
