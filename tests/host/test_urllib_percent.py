"""CPython 3.14.10 oracle and deterministic percent-conversion differential.

Run: python3.14 tests/host/test_urllib_percent.py [--vm build-debug/src/clovervm]
The VM imports its real urllib package; no protocol adapters are used.
"""
import argparse
import importlib.util
import inspect
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import urllib
import urllib.parse

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--vm', type=Path)
args = parser.parse_args()
assert sys.version_info[:3] == (3, 14, 10), sys.version
reference = urllib.parse
spec = importlib.util.spec_from_file_location('managed_url_percent', root / 'stdlib/urllib/parse.py')
managed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(managed)
for name in managed.__all__:
    assert inspect.signature(getattr(managed, name)) == inspect.signature(getattr(reference, name))
for suffix in ('', '_callbacks'):
    path = root / ('tests/python/test_urllib_percent' + suffix + '.py')
    source = compile(path.read_text(), str(path), 'exec')
    for module in (reference, managed):
        urllib.parse = module
        try:
            exec(source, {})
        finally:
            urllib.parse = reference

rng = random.Random(3141031)
blocks = []


def compare(name, *values, **options):
    expression = 'parse.' + name + '(' + ', '.join(
        [repr(v) for v in values] + [f'{k}={v!r}' for k, v in options.items()]) + ')'
    try:
        expected = getattr(reference, name)(*values, **options)
    except (TypeError, ValueError, AttributeError, LookupError) as error:
        try:
            getattr(managed, name)(*values, **options)
        except type(error) as actual:
            assert type(actual) is type(error), (expression, actual, error)
        else:
            raise AssertionError(('missing error', expression, error))
        blocks.append('try:\n    ' + expression + '\nexcept ' + type(error).__name__ +
                      ':\n    pass\nelse:\n    raise AssertionError("missing error")')
    else:
        actual = getattr(managed, name)(*values, **options)
        assert type(actual) is type(expected) and actual == expected, (expression, actual, expected)
        blocks.append('assert ' + expression + ' == ' + repr(expected))


for ch in range(256):
    compare('quote_from_bytes', bytes([ch]))
    compare('unquote_to_bytes', '%' + f'{ch:02x}')
    compare('unquote_to_bytes', '%' + f'{ch:02X}')
for a in b'%0129aAfFxZ\x00\xff':
    for b in b'%0129aAfFxZ\x00\xff':
        compare('unquote_to_bytes', bytes([37, a, b]))
for n in list(range(65)) + [127, 128, 255, 256, 1024]:
    raw = bytes(rng.randrange(256) for _ in range(n))
    for data in (raw, bytearray(raw)):
        for safe in ('', '/', '+ %', '\u00e9', b'\xff/ ', [0, 47, 255, 1000]):
            compare('quote_from_bytes', data, safe)
        compare('quote', data)
        compare('quote_plus', data)
        compare('unquote_to_bytes', data)
    compare('unquote_to_bytes', reference.quote_from_bytes(raw))
for _ in range(180):
    text = ''.join(rng.choice('abc /+%\x00\u00e9\u6f22\U0001f600') for _ in range(rng.randrange(30)))
    for name in ('quote', 'quote_plus'):
        compare(name, text)
        compare(name, text, safe=b'/+', encoding='utf-8', errors='strict')
        compare(name, text, encoding='latin-1')
    compare('unquote_to_bytes', text)
for name in ('quote', 'quote_plus'):
    compare(name, '\ud800')
    compare(name, 'a', encoding=3)
    compare(name, 'a', errors=3)
    compare(name, b'', encoding='utf-8')
    compare(name, b'', errors='strict')
    compare(name, '', safe=None, encoding=3, errors=3)
for name, data in (('quote_from_bytes', b''), ('quote_from_bytes', b'a'), ('quote', b'a')):
    for safe in (None, [-1], [1.5], [128, 1000000000000000000000000000000]):
        compare(name, data, safe)
for invalid in (None, 1, [], ()):
    compare('unquote_to_bytes', invalid)
if args.vm:
    with tempfile.TemporaryDirectory(prefix='clover-url-differential-') as directory:
        script = Path(directory) / 'batch.py'
        for offset in range(0, len(blocks), 100):
            script.write_text('from urllib import parse\n' + '\n'.join(blocks[offset:offset + 100]) + '\n')
            subprocess.run([str(args.vm.resolve()), str(script)], cwd=root, check=True)
print(f'PASS: shared assertions, signatures and {len(blocks)} differential cases; VM={bool(args.vm)}')
