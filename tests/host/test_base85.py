"""Deterministic Base85 differential against CPython 3.14.10.

Run: python3.14 tests/host/test_base85.py [--vm build-release/src/clovervm]
The optional VM checks use production imports and small assertion batches to
stay below CloverVM's per-code-object constant-table limit. General buffers,
subclass hooks and custom conversions are outside the agreed slice.
"""
import argparse
import base64
import importlib.util
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
for filename in ('test_base85_module.py', 'test_base85_errors.py', 'test_base85_callbacks.py'):
    source = compile((root / 'tests/python' / filename).read_text(), filename, 'exec')
    exec(source, {})
    sys.modules['base64'] = managed
    try:
        exec(source, {})
    finally:
        sys.modules['base64'] = base64
# Limits test asserts intentional differences and runs only on the managed code.
sys.modules['base64'] = managed
try:
    exec(compile((root / 'tests/python/test_base85_limits.py').read_text(),
                 'test_base85_limits.py', 'exec'), {})
finally:
    sys.modules['base64'] = base64

rng = random.Random(851410)
vm_blocks = []
cases = 0


def compare(name, data, *options):
    global cases
    call = getattr(base64, name)
    actual_call = getattr(managed, name)
    expression = f'base64.{name}({data!r}' + ''.join(f', {x!r}' for x in options) + ')'
    try:
        expected = call(data, *options)
    except (TypeError, ValueError) as expected_error:
        try:
            actual_call(data, *options)
        except type(expected_error) as actual_error:
            assert type(actual_error) is type(expected_error)
            assert actual_error.args == expected_error.args, (expression, actual_error, expected_error)
        else:
            raise AssertionError(('missing error', expression))
        vm_blocks.append('try:\n    ' + expression + '\nexcept ' + type(expected_error).__name__ +
                         ' as e:\n    assert str(e) == ' + repr(str(expected_error)) +
                         '\nelse:\n    raise AssertionError("missing error")')
    else:
        actual = actual_call(data, *options)
        assert type(actual) is bytes and actual == expected, (expression, actual, expected)
        vm_blocks.append('assert ' + expression + ' == ' + repr(expected))
    cases += 1


# Independent encode/decode oracle, including every one-byte input and tails.
for n in list(range(129)) + [255, 256, 257, 1023, 1024, 1025]:
    raw = bytes(rng.randrange(256) for _ in range(n))
    for pad in (False, True):
        compare('b85encode', raw, pad)
        compare('b85decode', base64.b85encode(raw, pad))
for value in range(256):
    compare('b85encode', bytes([value]))
    compare('b85decode', bytes([value]))
for raw in (bytes(range(256)), bytes(range(255, -1, -1))):
    compare('b85encode', raw)
    compare('b85decode', base64.b85encode(raw))
# Random independent byte strings and alphabet-only inputs, not just roundtrips.
alphabet = base64._b85alphabet
for _ in range(1500):
    raw = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 60)))
    compare('b85decode', raw)
    digits = bytes(rng.choice(alphabet) for _ in range(rng.randrange(1, 60)))
    compare('b85decode', digits)
# Sweep across the 32-bit limit, including short overflowing tails.
for word in range(4294967295 - 32, 4294967295 + 33):
    digits = bytearray(5)
    remainder = word
    for pos in range(4, -1, -1):
        digits[pos] = alphabet[remainder % 85]
        remainder //= 85
    compare('b85decode', bytes(digits))
    for length in range(1, 5):
        compare('b85decode', bytes(digits[:length]))
# Alphabet errors must win over overflow within a hunk, not an earlier hunk.
for pos in range(15):
    raw = bytearray(b'~~~~~~~~~~~~~~~')
    raw[pos] = 32
    compare('b85decode', bytes(raw))

if args.vm:
    vm = args.vm.resolve()
    with tempfile.TemporaryDirectory(prefix='clover-b85-differential-') as directory:
        script = Path(directory) / 'batch.py'
        for offset in range(0, len(vm_blocks), 20):
            script.write_text('import base64\n' + '\n'.join(vm_blocks[offset:offset + 20]) + '\n')
            subprocess.run([str(vm), str(script)], cwd=root, check=True)
print(f'PASS: {cases} independent differential cases; VM={bool(args.vm)}')
