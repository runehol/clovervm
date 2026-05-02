#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


OPCODE_SYMBOL_RE = re.compile(
    r"^(?P<address>[0-9a-fA-F]+)\s+\S+\s+"
    r"(?P<demangled>cl::(?P<name>op_[A-Za-z0-9_]+)\(.*\))$"
)
INSTRUCTION_RE = re.compile(
    r"^\s*(?:[0-9a-fA-F]+:)?\s*(?:[0-9a-fA-F]{8,16}\s+)?"
    r"(?P<mnemonic>[A-Za-z.][A-Za-z0-9_.]*)\s*(?P<operands>.*)$"
)


@dataclass(frozen=True)
class OpcodeSymbol:
    name: str
    mangled: str
    demangled: str


@dataclass(frozen=True)
class CheckResult:
    symbol: OpcodeSymbol
    has_frame_setup: bool
    frame_setup_instructions: tuple[str, ...]
    entry_instructions: tuple[str, ...]


def run_command(argv: list[str]) -> str:
    try:
        return subprocess.check_output(argv, text=True, stderr=subprocess.STDOUT)
    except FileNotFoundError:
        raise RuntimeError(f"could not find executable: {argv[0]}") from None
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(
            f"command failed with exit code {exc.returncode}: {' '.join(argv)}\n{exc.output}"
        ) from None


def find_opcode_symbols(binary: Path, nm: str) -> list[OpcodeSymbol]:
    raw_symbols = run_command([nm, "-an", str(binary)]).splitlines()
    demangled_symbols = run_command([nm, "-an", "-C", str(binary)]).splitlines()
    if len(raw_symbols) != len(demangled_symbols):
        raise RuntimeError("nm output changed between raw and demangled symbol reads")

    symbols: list[OpcodeSymbol] = []
    for raw, demangled in zip(raw_symbols, demangled_symbols):
        if "(.cold" in demangled:
            continue
        match = OPCODE_SYMBOL_RE.match(demangled)
        if not match:
            continue
        raw_parts = raw.split()
        if len(raw_parts) < 3:
            continue
        symbols.append(
            OpcodeSymbol(
                name=match.group("name"),
                mangled=raw_parts[2],
                demangled=match.group("demangled"),
            )
        )
    return sorted(symbols, key=lambda symbol: symbol.name)


def read_required_handlers(path: Path) -> set[str]:
    required: set[str] = set()
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise RuntimeError(f"could not read required handler list {path}: {exc}") from None

    for line_number, line in enumerate(lines, 1):
        text = line.split("#", 1)[0].strip()
        if not text:
            continue
        if not re.fullmatch(r"op_[A-Za-z0-9_]+", text):
            raise RuntimeError(f"{path}:{line_number}: invalid opcode handler name: {text}")
        required.add(text)
    return required


def disassemble_symbol(binary: Path, objdump: str, symbol: OpcodeSymbol) -> list[str]:
    output = run_command(
        [objdump, "-d", f"--disassemble-symbols={symbol.mangled}", str(binary)]
    )
    instructions: list[str] = []
    in_symbol = False
    for line in output.splitlines():
        if f"<{symbol.mangled}>" in line:
            in_symbol = True
            continue
        if in_symbol and line.strip():
            instructions.append(line.rstrip())
    return instructions


def is_frame_setup_instruction(line: str) -> bool:
    match = INSTRUCTION_RE.match(line)
    if not match:
        return False
    mnemonic = match.group("mnemonic").lower()
    operands = match.group("operands").lower()

    if mnemonic in {"push", "pushq"}:
        return True
    if mnemonic in {"stp", "str", "stur"} and "[sp" in operands:
        return True
    return False


def check_symbol(
    binary: Path, objdump: str, symbol: OpcodeSymbol, entry_instruction_limit: int
) -> CheckResult:
    instructions = disassemble_symbol(binary, objdump, symbol)
    entry_instructions = tuple(instructions[:entry_instruction_limit])
    frame_setup = tuple(
        instruction
        for instruction in entry_instructions
        if is_frame_setup_instruction(instruction)
    )
    return CheckResult(
        symbol=symbol,
        has_frame_setup=bool(frame_setup),
        frame_setup_instructions=frame_setup,
        entry_instructions=entry_instructions,
    )


def print_result(result: CheckResult, required: bool) -> None:
    status = "FRAME" if result.has_frame_setup else "noframe"
    marker = "required" if required else "observed"
    print(f"{status:7} {marker:8} {result.symbol.name}")
    for instruction in result.frame_setup_instructions:
        print(f"         frame setup: {instruction.strip()}")


def print_failure(result: CheckResult) -> None:
    print(
        f"Hot path opcode handler {result.symbol.name} is expected to enter "
        "without a stack frame, but the release binary sets one up."
    )
    print("Entry disassembly:")
    for instruction in result.entry_instructions:
        marker = "  <-- stack-frame setup" if instruction in result.frame_setup_instructions else ""
        print(f"    {instruction.strip()}{marker}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check whether Clover opcode handlers set up stack frames on entry."
    )
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--required-list", type=Path)
    parser.add_argument("--nm", default="nm")
    parser.add_argument("--objdump", default="objdump")
    parser.add_argument("--entry-instruction-limit", type=int, default=8)
    parser.add_argument(
        "--dump-all",
        action="store_true",
        help="Print every opcode handler, not just required failures.",
    )
    parser.add_argument(
        "--warn-only",
        action="store_true",
        help="Print errors but exit successfully.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.binary.exists():
        print(f"error: binary does not exist: {args.binary}", file=sys.stderr)
        return 0 if args.warn_only else 2

    try:
        symbols = find_opcode_symbols(args.binary, args.nm)
        by_name = {symbol.name: symbol for symbol in symbols}
        required = (
            read_required_handlers(args.required_list) if args.required_list else set()
        )
        missing = sorted(required - set(by_name))
        if missing:
            print("error: required opcode handlers were not found:", file=sys.stderr)
            for name in missing:
                print(f"  {name}", file=sys.stderr)
            return 0 if args.warn_only else 1

        failures: list[CheckResult] = []
        for symbol in symbols:
            result = check_symbol(
                args.binary, args.objdump, symbol, args.entry_instruction_limit
            )
            is_required = symbol.name in required
            if args.dump_all:
                print_result(result, is_required)
            if is_required and result.has_frame_setup:
                failures.append(result)

        if failures:
            for result in failures:
                print_failure(result)
            print(
                f"\n{'warning' if args.warn_only else 'error'}: "
                f"{len(failures)} hot path opcode handler(s) expected to be "
                "frame-free set up a stack frame"
            )
            return 0 if args.warn_only else 1

        if not args.warn_only:
            print(
                f"checked {len(required)} required opcode handlers "
                f"({len(symbols)} opcode handlers discovered)"
            )
        return 0
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 0 if args.warn_only else 2


if __name__ == "__main__":
    raise SystemExit(main())
