"""Compare float.fromhex with CPython 3.14. Run with Python and a VM path."""

import math
import pathlib
import random
import subprocess
import sys
import tempfile


def inputs():
    yield from (
        "  +INF ", "-infinity", "NaN", "-nan", "1,8", "1\x00", "\uff11",
        "0x.p0", "1p+ ", "1p1024junk", "1p-1076junk",
        "1p1024\ud800", "-" + "0p" + "9" * 2048, "1p" + "9" * 2048,
        "1" + "0" * 4096 + "p-16384", "." + "0" * 4096 + "1p16388",
    )
    rng = random.Random(163)
    top_exponents = (-1076, -1075, -1074, -1073, -1023, -1022, -1, 0,
                     1, 1022, 1023, 1024)
    # Sample both sides of exact midpoints, including the carry to infinity.
    for top in top_exponents[1:-1]:
        keep = min(53, top + 1075)
        low = 1 << (keep - 1) if keep else 0
        for significand in sorted({low, (1 << keep) - 1}):
            for delta in (-1, 0, 1):
                coefficient = (2 * significand + 1) * (1 << 80) + delta
                exponent = top - keep - 80
                for sign in ("", "-"):
                    yield f"{sign}0x{coefficient:x}p{exponent}"

    for i in range(2400):
        width = rng.choice((1, 4, 51, 52, 53, 54, 57, 127, 300, 4096))
        coefficient = rng.getrandbits(width) | (1 << (width - 1))
        digits = "0" * rng.randrange(8) + format(coefficient, "x")
        point = rng.randrange(len(digits) + 1)
        top = (rng.choice(top_exponents) if i % 2
               else rng.randrange(-1100, 1050))
        exponent = top - (width - 1) + 4 * (len(digits) - point)
        sign = rng.choice(("", "+", "-"))
        prefix = rng.choice(("", "0x", "0X"))
        marker = rng.choice(("p", "P"))
        yield f" {sign}{prefix}{digits[:point]}.{digits[point:]}{marker}{exponent:+d}\t"


def expected_literal(text):
    try:
        number = float.fromhex(text)
    except (ValueError, OverflowError) as error:
        return type(error).__name__
    sign = math.copysign(1.0, number)
    if math.isnan(number):
        return repr(("nan", sign))
    if math.isinf(number):
        return repr(("inf", sign))
    return repr((number.as_integer_ratio(), sign))


CHECK = """\
import math

def check(text, expected):
    try:
        result = float.fromhex(text)
    except Exception as error:
        assert error.__class__ is expected, repr(text)
        return
    assert math.copysign(1.0, result) == expected[1], repr(text)
    if expected[0] == "nan":
        assert math.isnan(result), repr(text)
    elif expected[0] == "inf":
        assert math.isinf(result), repr(text)
    else:
        assert result.as_integer_ratio() == expected[0], repr(text)
"""


def main():
    vm = pathlib.Path(sys.argv[1]).resolve()
    cases = list(inputs())
    # Stay below the VM's constant-table limit.
    with tempfile.TemporaryDirectory(prefix="clover-float-fromhex-") as directory:
        source = pathlib.Path(directory) / "fromhex.py"
        for start in range(0, len(cases), 32):
            batch = cases[start:start + 32]
            source.write_text(CHECK + "\n".join(
                f"check({ascii(text)}, {expected_literal(text)})"
                for text in batch
            ) + "\n", encoding="ascii")
            result = subprocess.run([str(vm), str(source)], capture_output=True,
                                    text=True, timeout=60)
            if result.returncode:
                raise AssertionError(
                    f"Fixture batch {start}: {result.stdout}\n{result.stderr}"
                )
    print(f"{len(cases)} float.fromhex differential cases passed")


if __name__ == "__main__":
    main()
