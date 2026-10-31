"""Exact binary64 differential checks. Run with Python 3.14 and a VM path."""

import math
import pathlib
import random
import struct
import subprocess
import sys
import tempfile


def decoded_ratio(bits):
    """Decode IEEE binary64 using integer arithmetic independently of frexp."""
    exponent = (bits >> 52) & 2047
    significand = bits & ((1 << 52) - 1)
    if exponent == 2047:
        return None
    if exponent:
        significand |= 1 << 52
        shift = exponent - 1075
    else:
        shift = -1074
    numerator = significand << max(shift, 0)
    denominator = 1 << max(-shift, 0)
    divisor = math.gcd(numerator, denominator)
    numerator //= divisor
    denominator //= divisor
    if bits >> 63:
        numerator = -numerator
    return numerator, denominator


def main():
    vm = pathlib.Path(sys.argv[1]).resolve()
    rng = random.Random(397)
    patterns = [0, 1 << 63]
    # Every finite exponent with minimum/maximum mantissas and both signs.
    for exponent in range(2047):
        for mantissa in (0, 1, (1 << 52) - 1):
            for sign in (0, 1):
                patterns.append((sign << 63) | (exponent << 52) | mantissa)
    patterns.extend(rng.getrandbits(64) for _ in range(4000))
    fixtures = []
    for bits in patterns:
        expected = decoded_ratio(bits)
        if expected is None:
            continue
        value = struct.unpack('>d', bits.to_bytes(8, 'big'))[0]
        assert value.as_integer_ratio() == expected, hex(bits)
        fixtures.append((value, expected))
    # Keep each script below the VM constant-table limit. Check exact integers,
    # never n/d roundtrips.
    with tempfile.TemporaryDirectory(prefix='clover-float-ratio-') as directory:
        source = pathlib.Path(directory) / 'ratios.py'
        for start in range(0, len(fixtures), 32):
            batch = fixtures[start:start + 32]
            lines = ['cases = [']
            lines.extend(f'({value!r}, {pair!r}),' for value, pair in batch)
            lines.extend([
                ']',
                'for case in cases:',
                '    assert case[0].as_integer_ratio() == case[1]',
                '    assert float.as_integer_ratio(case[0]) == case[1]',
            ])
            source.write_text('\n'.join(lines) + '\n')
            result = subprocess.run([str(vm), str(source)], capture_output=True,
                                    text=True, timeout=60)
            if result.returncode:
                raise AssertionError(
                    f'Fixture batch {start}: {result.stdout}\n{result.stderr}')
    print(f'{len(fixtures)} exact bit-level/CPython/VM ratio checks passed')


if __name__ == '__main__':
    main()
