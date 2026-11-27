"""Compare the fixed-width struct slice with CPython 3.14.10."""

import argparse
from difflib import unified_diff
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile


PRELUDE = """import struct
import sys

def show(call, *args):
    try:
        result = call(*args)
    except struct.error as exc:
        print("error", str(exc))
    else:
        print("ok", result.hex() if isinstance(result, bytes) else repr(result))

def check_pack_into(call, prefix, values, wire):
    for offset in (1, -len(wire) - 1):
        target = bytearray(b"<" + bytes(len(wire)) + b">")
        show(call, *(prefix + (target, offset) + values))
        print(target.hex())

def check(format, values, wire):
    spec = struct.Struct(format)
    print(repr(spec.format), spec.size, struct.calcsize(format))
    show(spec.pack, *values)
    show(struct.pack, format, *values)
    show(spec.unpack, wire)
    show(struct.unpack, format, bytearray(wire))
    show(spec.unpack, wire + b"x")
    show(struct.unpack, format, wire + b"x")
    framed = b"<" + wire + b">"
    check_pack_into(spec.pack_into, (), values, wire)
    check_pack_into(struct.pack_into, (format,), values, wire)
    show(spec.unpack_from, framed, 1)
    show(struct.unpack_from, format, bytearray(framed), -len(wire) - 1)
    if wire:
        print(list(spec.iter_unpack(wire + wire)))
        print(list(struct.iter_unpack(format, wire + wire)))

def check_overflow(format, value):
    show(struct.Struct(format).pack, value)
    show(struct.pack, format, value)
    target = bytearray(b"************")
    show(struct.pack_into, format, target, 1, value)
    end = 1 + struct.calcsize(format)
    foreign_prefixes = ">!" if sys.byteorder == "little" else "<"
    if format[0] in foreign_prefixes and format[-1] in "qQ":
        # CPython's opposite-endian encoder can write bytes before overflow.
        if sys.implementation.name == "clovervm":
            assert target[1:end] == bytes(end - 1)
        print(target[:1].hex(), target[end:].hex())
    else:
        print(target.hex())
"""
WIDTHS = {"b": 1, "h": 2, "i": 4, "l": 4, "q": 8}
OPTIONS = tuple(WIDTHS) + tuple(code.upper() for code in WIDTHS)


def limits(code):
    bits = WIDTHS[code.lower()] * 8
    if code.islower():
        return -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    return 0, (1 << bits) - 1


def valid_case(format, values):
    wire = struct.pack(format, *values)
    return f"check({format!r}, {values!r}, {wire!r})"


def cases():
    randomizer = random.Random(0x53545255)
    for order in "<>!=":
        for code in OPTIONS:
            low, high = limits(code)
            yield valid_case(order + "4" + code, (low, high, 0, True))
            for value in (low - 1, high + 1, -(1 << 130), 1 << 130):
                yield f"check_overflow({order + code!r}, {value!r})"
        for _ in range(25):
            fields = [order]
            values = []
            for _ in range(randomizer.randrange(1, 12)):
                code = randomizer.choice(OPTIONS + ("x",))
                count = randomizer.randrange(5)
                fields.append(f"{count}{code}")
                if code != "x":
                    values.extend(randomizer.randint(*limits(code)) for _ in range(count))
            yield valid_case(" ".join(fields), tuple(values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vm", type=Path, required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 10), sys.version
    root = Path(__file__).resolve().parents[2]
    vm = args.vm.resolve()
    fixture = root / "tests/python/test_struct.py"
    for executable in (sys.executable, vm):
        subprocess.run([executable, fixture], cwd=root, check=True)

    entries = list(cases())
    with tempfile.TemporaryDirectory(prefix="clover-struct-differential-") as folder:
        script = Path(folder) / "cases.py"
        for offset in range(0, len(entries), 20):
            batch = entries[offset:offset + 20]
            script.write_text(PRELUDE + "\n".join(batch) + "\n", encoding="ascii")
            results = [
                subprocess.check_output([executable, script], cwd=root, text=True)
                for executable in (sys.executable, vm)
            ]
            if results[0] != results[1]:
                diff = unified_diff(
                    results[0].splitlines(), results[1].splitlines(),
                    fromfile="CPython", tofile="CloverVM",
                )
                raise AssertionError("\n".join(batch + list(diff)))
    print(f"{len(entries)} differential cases passed")


if __name__ == "__main__":
    main()
