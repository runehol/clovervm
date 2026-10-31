"""CPython 3.14.10 oracle for shared explicit-separator string search.

Run: python3.14 tests/host/test_string_separator_search.py [path/to/clovervm]
Whitespace, custom index conversion, and general protocol changes are excluded.
"""
from pathlib import Path
import random
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
    runpy.run_path(str(ROOT / 'tests/python/string_separator_search.py'))
    rng = random.Random(342786)
    alphabet = '\0a\u0100\u0200\u0180\u0280\ud800\uffff\U00010000\U0010ffff'
    cases = []
    for width in (1, 2, 7, 8, 9, 16, 17, 32, 64, 129):
        for trial in range(12):
            separator = ''.join(rng.choice(alphabet) for _ in range(width))
            if trial % 3 == 0:
                separator = ('\u0100\u0200' * width)[:width]
            middle = ''.join(rng.choice(alphabet) for _ in range(width + 7))
            for source in ('', middle, separator + middle + separator,
                           separator + '\u0200\u0100' + separator):
                for limit in (-2, -1, 0, 1, 2, 3):
                    cases.append((source, separator, limit,
                                  source.split(separator, limit),
                                  source.rsplit(separator, limit),
                                  source.partition(separator),
                                  source.rpartition(separator)))
    # Keep each generated module below the VM's constant-table index limit.
    with tempfile.TemporaryDirectory(prefix='clover-string-search-') as folder:
        path = Path(folder) / 'oracle.py'
        for offset in range(0, len(cases), 24):
            trace = 'cases = ' + ascii(cases[offset:offset + 24]) + """
for case in cases:
    source = case[0]
    separator = case[1]
    limit = case[2]
    assert source.split(separator, limit) == case[3]
    assert source.rsplit(separator, limit) == case[4]
    assert source.partition(separator) == case[5]
    assert source.rpartition(separator) == case[6]
"""
            path.write_text(trace)
            actual = subprocess.run([str(binary), str(path)], cwd=ROOT,
                                    capture_output=True, text=True)
            if actual.returncode:
                raise AssertionError(
                    f'Oracle batch {offset // 24}: CloverVM exited with '
                    f'{actual.returncode}:\n'
                    f'{actual.stdout[-1000:]}{actual.stderr}')
    print(f'CPython 3.14.10: {len(cases) * 4} matching string split/partition results')


if __name__ == '__main__':
    main()
