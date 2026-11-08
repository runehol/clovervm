#!/usr/bin/env python3
"""Run bounded GC differential properties with host CPython and Hypothesis."""

import argparse
import json
import math
from pathlib import Path
import platform
import sys

import hypothesis
from hypothesis import Phase, given, seed as hypothesis_seed, settings
from hypothesis.errors import HypothesisException

from compare import ComparisonFailure, HeapMismatch, RunnerFailure, assert_equivalent
from heap_strategies import valid_heaps


def run_campaign(*, cpp, lean, examples=100, max_objects=20, seed=None,
                 algorithm="both", timeout=15, failure_dir="gc-differential-failures"):
    """Run separate collector properties and return actual comparison counts.

    Reuse and generation are enabled; shrinking remains a separate slice.
    Every comparator failure is retained, including confirmation attempts.
    """
    if examples <= 0 or max_objects < 0:
        raise ValueError("examples must be positive and max_objects nonnegative")
    if algorithm not in ("mark", "evacuate", "both"):
        raise ValueError("algorithm must be mark, evacuate or both")
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be finite and positive")
    selected = ("mark", "evacuate") if algorithm == "both" else (algorithm,)
    counts = {name: 0 for name in selected}
    options = settings(max_examples=examples, deadline=None, report_multiple_bugs=False,
                       phases=(Phase.reuse, Phase.generate))
    heaps = valid_heaps(max_objects=max_objects)

    def check(heap, name):
        counts[name] += 1
        try:
            assert_equivalent(heap, cpp=cpp, lean=lean, algorithm=name,
                              timeout=timeout, failure_dir=failure_dir)
        except ComparisonFailure as error:
            report_file = error.artifact_dir / "run.json"
            report = json.loads(report_file.read_text(encoding="utf-8"))
            report["campaign"] = {
                "examples": examples, "max_objects": max_objects, "seed": seed,
                "attempt": counts[name], "phases": [phase.name for phase in options.phases],
                "hypothesis_version": hypothesis.__version__, "python_version": platform.python_version(),
            }
            report_file.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            raise

    @options
    @given(heap=heaps)
    def mark(heap):
        check(heap, "mark")

    @options
    @given(heap=heaps)
    def evacuate(heap):
        check(heap, "evacuate")

    for name, property_test in (("mark", mark), ("evacuate", evacuate)):
        if name in selected:
            if seed is not None:
                property_test = hypothesis_seed(seed)(property_test)
            property_test()
    return counts


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", type=Path, required=True)
    parser.add_argument("--lean", type=Path, required=True)
    parser.add_argument("--algorithm", choices=("mark", "evacuate", "both"), default="both")
    parser.add_argument("--collection", choices=("full",), required=True)
    parser.add_argument("--examples", type=int, default=100)
    parser.add_argument("--max-objects", type=int, default=20)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--failure-dir", type=Path, default=Path("gc-differential-failures"))
    args = parser.parse_args(argv)
    try:
        counts = run_campaign(cpp=args.cpp, lean=args.lean, algorithm=args.algorithm,
                              examples=args.examples, max_objects=args.max_objects, seed=args.seed,
                              timeout=args.timeout, failure_dir=args.failure_dir)
    except HeapMismatch as error:
        print(f"gc_campaign: {error}", file=sys.stderr)
        return 1
    except (RunnerFailure, HypothesisException, OSError, ValueError) as error:
        print(f"gc_campaign: {error}", file=sys.stderr)
        return 2
    print("GC campaign passed: " + ", ".join(f"{name}={count}" for name, count in counts.items()) + " comparisons.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
