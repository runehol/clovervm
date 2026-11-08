#!/usr/bin/env python3
"""Host-CPython comparison of the C++ and Lean GC heap file runners."""

import argparse
from dataclasses import dataclass
import difflib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile


class ComparisonFailure(AssertionError):
    def __init__(self, message, artifact_dir):
        self.artifact_dir = artifact_dir
        super().__init__(f"{message}; reproduction: {artifact_dir}")


class HeapMismatch(ComparisonFailure):
    """Both runners succeeded, but their complete JSON snapshots differ."""


class RunnerFailure(ComparisonFailure):
    """At least one runner failed to produce a successful JSON snapshot."""


@dataclass
class RunResult:
    command: list[str]
    status: str
    stdout: bytes = b""
    stderr: bytes = b""
    returncode: int | None = None
    error: str | None = None
    canonical: str | None = None


def run_runner(executable, heap_file, algorithm, collection, timeout):
    command = [str(Path(executable).resolve()), "--algorithm", algorithm,
               "--collection", collection, str(heap_file)]
    try:
        process = subprocess.run(command, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        return RunResult(command, "timeout", error.stdout or b"", error.stderr or b"")
    except OSError as error:
        return RunResult(command, "launch_error", error=str(error))
    result = RunResult(command, "success", process.stdout, process.stderr, process.returncode)
    if process.returncode:
        result.status = "crashed" if process.returncode < 0 else "rejected"
    elif process.stderr:
        result.status = "unexpected_stderr"
    else:
        try:
            # Sorting dictionary keys ignores member order while retaining array
            # order and distinguishing Python's otherwise equal True and 1.
            result.canonical = json.dumps(json.loads(process.stdout), sort_keys=True,
                                          indent=2, allow_nan=False) + "\n"
        except ValueError as error:
            result.status = "invalid_json"
            result.error = str(error)
    return result


def preserve_failure(input_bytes, runs, outcome, algorithm, collection, timeout, failure_dir):
    failure_dir = Path(failure_dir).resolve()
    failure_dir.mkdir(parents=True, exist_ok=True)
    artifact = Path(tempfile.mkdtemp(prefix="failure-", dir=failure_dir))
    heap_file = artifact / "input.json"
    heap_file.write_bytes(input_bytes)
    report = {"outcome": outcome, "algorithm": algorithm, "collection": collection,
              "timeout": timeout, "runners": {}}
    for name, run in runs.items():
        (artifact / f"{name}.stdout").write_bytes(run.stdout)
        (artifact / f"{name}.stderr").write_bytes(run.stderr)
        report["runners"][name] = {
            # Replace the temporary input path with the preserved input so the
            # recorded command can be replayed after this invocation returns.
            "command": [*run.command[:-1], str(heap_file)],
            "status": run.status, "returncode": run.returncode, "error": run.error,
        }
    if outcome == "mismatch":
        diff = difflib.unified_diff(runs["cpp"].canonical.splitlines(keepends=True),
                                    runs["lean"].canonical.splitlines(keepends=True),
                                    fromfile="cpp", tofile="lean")
        (artifact / "difference.diff").write_text("".join(diff), encoding="utf-8")
    (artifact / "run.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return artifact


def _assert_equivalent(input_bytes, *, cpp, lean, algorithm, collection, timeout, failure_dir):
    if algorithm not in ("mark", "evacuate"):
        raise ValueError("algorithm must be mark or evacuate")
    if collection not in ("full", "young") or (collection == "young" and algorithm != "evacuate"):
        raise ValueError("young collection supports evacuation only; collection must be full or young")
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be finite and positive")
    with tempfile.TemporaryDirectory(prefix="gc-compare-") as temp:
        heap_file = Path(temp) / "input.json"
        heap_file.write_bytes(input_bytes)
        runs = {name: run_runner(exe, heap_file, algorithm, collection, timeout)
                for name, exe in (("cpp", cpp), ("lean", lean))}
    failures = [f"{name}: {run.status}" for name, run in runs.items() if run.status != "success"]
    if failures:
        artifact = preserve_failure(input_bytes, runs, "runner_failure", algorithm, collection, timeout, failure_dir)
        raise RunnerFailure(", ".join(failures), artifact)
    if runs["cpp"].canonical != runs["lean"].canonical:
        artifact = preserve_failure(input_bytes, runs, "mismatch", algorithm, collection, timeout, failure_dir)
        raise HeapMismatch("heap snapshots differ", artifact)


def assert_equivalent(heap, *, cpp, lean, algorithm, collection="full", timeout=15,
                      failure_dir="gc-differential-failures"):
    """Assert equality for a generated heap dictionary; raise typed failures.

    This standard-library API is intended for host-CPython property tests.
    Runners own heap validation. Every failed attempt gets a unique reproduction
    directory, so later attempts or shrinking cannot overwrite the original.
    """
    input_bytes = (json.dumps(heap, allow_nan=False) + "\n").encode("utf-8")
    _assert_equivalent(input_bytes, cpp=cpp, lean=lean, algorithm=algorithm,
                       collection=collection, timeout=timeout, failure_dir=failure_dir)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", type=Path, required=True)
    parser.add_argument("--lean", type=Path, required=True)
    parser.add_argument("--algorithm", choices=("mark", "evacuate"), required=True)
    parser.add_argument("--collection", choices=("full", "young"), required=True)
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--failure-dir", type=Path, default=Path("gc-differential-failures"))
    parser.add_argument("file", type=Path)
    args = parser.parse_args(argv)
    try:
        # Keep exact input bytes for reproduction, including rejected inputs.
        _assert_equivalent(args.file.read_bytes(), cpp=args.cpp, lean=args.lean,
                           algorithm=args.algorithm, collection=args.collection, timeout=args.timeout,
                           failure_dir=args.failure_dir)
    except HeapMismatch as error:
        print(f"gc_compare: {error}", file=sys.stderr)
        return 1
    except (RunnerFailure, OSError, ValueError) as error:
        print(f"gc_compare: {error}", file=sys.stderr)
        return 2
    print("GC heaps match.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
