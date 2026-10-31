"""Deterministic Ascii85 differential against CPython 3.14.10.

Run: python3.14 tests/host/test_ascii85.py [--vm build-release/src/clovervm]
Shared assertions run against native CPython and the production managed source.
Optional VM batches import the production module; no protocol adapters are used.
"""
import argparse
import base64
import importlib.util
import inspect
from pathlib import Path
import random
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--vm', type=Path)
args = parser.parse_args()
assert sys.version_info[:3] == (3, 14, 10), sys.version
spec = importlib.util.spec_from_file_location('managed_base64', root / 'stdlib/base64.py')
managed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(managed)
for name in ('a85encode', 'a85decode'):
    assert inspect.signature(getattr(managed, name)) == inspect.signature(getattr(base64, name))
for suffix in ('module', 'errors', 'callbacks', 'limits'):
    filename = 'test_ascii85_' + suffix + '.py'
    source = compile((root / 'tests/python' / filename).read_text(), filename, 'exec')
    if suffix != 'limits':
        exec(source, {})
    sys.modules['base64'] = managed
    try:
        exec(source, {})
    finally:
        sys.modules['base64'] = base64

rng = random.Random(85141031)
vm_blocks = []


def compare(name, data, **options):
    expression = f'base64.{name}({data!r}' + ''.join(f', {k}={v!r}' for k, v in options.items()) + ')'
    try:
        expected = getattr(base64, name)(data, **options)
    except (TypeError, ValueError) as expected_error:
        try:
            getattr(managed, name)(data, **options)
        except type(expected_error) as actual_error:
            assert type(actual_error) is type(expected_error), expression
            if isinstance(expected_error, ValueError):
                assert actual_error.args == expected_error.args, (expression, actual_error, expected_error)
        else:
            raise AssertionError(('missing error', expression))
        block = 'try:\n    ' + expression + '\nexcept ' + type(expected_error).__name__ + ' as e:\n    pass'
        if isinstance(expected_error, ValueError):
            block += '\n    assert str(e) == ' + repr(str(expected_error))
        block += '\nelse:\n    raise AssertionError("missing error")'
        vm_blocks.append(block)
    else:
        actual = getattr(managed, name)(data, **options)
        assert type(actual) is bytes and actual == expected, (expression, actual, expected)
        vm_blocks.append('assert ' + expression + ' == ' + repr(expected))


# Independent oracle outputs, every one-byte input and wrapping/tail boundaries.
for value in range(256):
    compare('a85encode', bytes([value]))
    compare('a85decode', bytes([value]))
for n in list(range(65)) + [255, 256, 257, 1023, 1024, 1025]:
    for raw in (bytes(rng.randrange(256) for _ in range(n)), bytes(n), b' ' * n):
        for pad in (False, True):
            for foldspaces in (False, True):
                compare('a85encode', raw, pad=pad, foldspaces=foldspaces)
        for adobe in (False, True):
            for width in (-2, 0, 1, 2, 3, 4, 5, 7, 10):
                compare('a85encode', raw, adobe=adobe, wrapcol=width)
        encoded = base64.a85encode(raw)
        compare('a85decode', encoded)
        compare('a85decode', b'<~' + encoded + b'~>', adobe=True)
for width in (2**60-1, 2**60, 2**63, 2**64, 10**100):
    for raw in (b'', b'foo'):
        for adobe in (False, True):
            compare('a85encode', raw, wrapcol=width, adobe=adobe)
            compare('a85encode', raw, wrapcol=-width, adobe=adobe)
for _ in range(1000):
    raw = bytes(rng.randrange(256) for _ in range(rng.randrange(60)))
    digits = bytes(rng.randrange(33, 118) for _ in range(rng.randrange(60)))
    compare('a85decode', raw, foldspaces=bool(rng.randrange(2)))
    compare('a85decode', digits)
for word in range(4294967295 - 32, 4294967295 + 33):
    digits = bytearray(5)
    for pos in range(4, -1, -1):
        digits[pos] = 33 + word % 85
        word //= 85
    for length in range(1, 6):
        compare('a85decode', bytes(digits[:length]))
for raw in (b'', b'~>', b'<~~>', b'<~z~>', b'<~z~>\n', b'z~>', b' <~z~>', b'!z', b'!y', b'y z', b'\fz'):
    for adobe in (False, True):
        for foldspaces in (False, True):
            for ignore in (b' \t\n\r\v', b'', b'!zy\f ', None, ' ', [32]):
                compare('a85decode', raw, adobe=adobe, foldspaces=foldspaces, ignorechars=ignore)
if args.vm:
    vm = args.vm.resolve()
    with tempfile.TemporaryDirectory(prefix='clover-a85-differential-') as directory:
        script = Path(directory) / 'batch.py'
        for offset in range(0, len(vm_blocks), 20):
            script.write_text('import base64\n' + '\n'.join(vm_blocks[offset:offset + 20]) + '\n')
            subprocess.run([str(vm), str(script)], cwd=root, check=True)
print(f'PASS: {len(vm_blocks)} independent Ascii85 differential cases; VM={bool(args.vm)}')
