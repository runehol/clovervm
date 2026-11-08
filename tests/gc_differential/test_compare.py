"""Run with host CPython: python3 -m unittest discover -s tests/gc_differential."""

import contextlib
import io
import json
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest

from compare import HeapMismatch, RunnerFailure, assert_equivalent, main


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).with_name("fixtures")
EMPTY = {"format": "clover-gc-heap", "version": 2, "roots": [],
         "objects": [], "external_weak": [], "remembered_strong": []}


class CompareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="gc-compare-test-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.failure_dir = self.directory / "failures"

    def runner(self, name, body):
        path = self.directory / name
        path.write_text(f"#!{sys.executable}\n{body}\n", encoding="utf-8")
        path.chmod(0o700)
        return path

    def output_runner(self, name, value):
        return self.runner(name, f"print({json.dumps(value)!r})")

    def compare(self, cpp, lean, **kwargs):
        return assert_equivalent(EMPTY, cpp=cpp, lean=lean, algorithm="evacuate",
                                 failure_dir=self.failure_dir, **kwargs)

    def report(self, error):
        return json.loads((error.artifact_dir / "run.json").read_text())

    def test_json_member_order_and_whitespace_are_ignored(self):
        cpp = self.output_runner("cpp", EMPTY)
        lean = self.runner("lean", f"print({json.dumps(dict(reversed(list(EMPTY.items()))), indent=2)!r})")
        self.assertIsNone(self.compare(cpp, lean))
        self.assertFalse(self.failure_dir.exists())

    def test_every_heap_observation_is_compared(self):
        baseline = json.loads((FIXTURES / "after.json").read_bytes())
        mutations = [
            lambda h: h["roots"].reverse(),
            lambda h: h["objects"].reverse(),
            lambda h: h["objects"][0]["state"].update(marked=False),
            lambda h: h["objects"][1]["state"]["header"].update(forwarded_to=0),
            lambda h: h["objects"][3].update(object_id="obj-0"),
            lambda h: h["objects"][3].update(allocation_id=0),
            lambda h: h["objects"][3]["strong"].reverse(),
            lambda h: h["objects"][3]["weak"][0].update(unit_id=99),
            lambda h: h["external_weak"][0].update(guards=[1, 0]),
            lambda h: h["external_weak"][0].update(payload={"ref": None}),
        ]
        # The reviewed fixture's roots are identical; use distinct cells so
        # reversing them tests array order rather than an unchanged value.
        baseline["roots"] = [{"imm": 7}, {"ref": 3}]
        cpp = self.output_runner("cpp", baseline)
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                changed = json.loads(json.dumps(baseline))
                mutate(changed)
                with self.assertRaises(HeapMismatch):
                    self.compare(cpp, self.output_runner("lean", changed))

    def test_equal_python_values_with_different_json_types_differ(self):
        for left, right in [(True, 1), (False, 0), (1, 1.0), (None, 0)]:
            with self.subTest(left=left, right=right):
                with self.assertRaises(HeapMismatch):
                    self.compare(self.output_runner("cpp", {"value": left}),
                                 self.output_runner("lean", {"value": right}))

    def test_failure_artifacts_are_replayable_and_never_overwritten(self):
        cpp = self.output_runner("cpp", EMPTY)
        lean = self.output_runner("lean", {**EMPTY, "roots": [{"imm": 7}]})
        with self.assertRaises(HeapMismatch) as first:
            self.compare(cpp, lean)
        artifact = first.exception.artifact_dir
        self.assertEqual(json.loads((artifact / "input.json").read_bytes()), EMPTY)
        self.assertEqual(json.loads((artifact / "cpp.stdout").read_bytes()), EMPTY)
        self.assertEqual((artifact / "cpp.stderr").read_bytes(), b"")
        self.assertIn('"imm": 7', (artifact / "difference.diff").read_text())
        report = self.report(first.exception)
        self.assertEqual(report["outcome"], "mismatch")
        self.assertEqual(report["algorithm"], "evacuate")
        for runner in report["runners"].values():
            self.assertEqual(runner["command"][1:5], ["--algorithm", "evacuate", "--collection", "full"])
            self.assertEqual(runner["command"][-1], str(artifact / "input.json"))
            replay = subprocess.run(runner["command"], capture_output=True, check=True)
            self.assertTrue(replay.stdout)
        original = (artifact / "run.json").read_bytes()
        with self.assertRaises(HeapMismatch) as second:
            self.compare(cpp, lean)
        self.assertNotEqual(artifact, second.exception.artifact_dir)
        self.assertEqual((artifact / "run.json").read_bytes(), original)

    def test_same_snapshot_file_is_passed_to_both_runners(self):
        body = "import json, sys\nprint(json.dumps({'path': sys.argv[-1], 'heap': json.load(open(sys.argv[-1]))}))"
        self.compare(self.runner("cpp", body), self.runner("lean", body))

    def test_rejection_is_not_agreement_even_if_both_reject(self):
        reject = "import sys\nsys.stdout.buffer.write(b'partial')\nsys.stderr.buffer.write(b'bad heap\\n')\nsys.exit(1)"
        with self.assertRaises(RunnerFailure) as caught:
            self.compare(self.runner("cpp", reject), self.runner("lean", reject))
        report = self.report(caught.exception)
        self.assertEqual(report["outcome"], "runner_failure")
        self.assertEqual([r["status"] for r in report["runners"].values()], ["rejected", "rejected"])
        self.assertEqual((caught.exception.artifact_dir / "cpp.stdout").read_bytes(), b"partial")
        self.assertEqual((caught.exception.artifact_dir / "cpp.stderr").read_bytes(), b"bad heap\n")

    def test_crash_is_distinct_from_rejection(self):
        crash = self.runner("cpp", "import os, signal\nos.kill(os.getpid(), signal.SIGTERM)")
        with self.assertRaises(RunnerFailure) as caught:
            self.compare(crash, self.output_runner("lean", EMPTY))
        result = self.report(caught.exception)["runners"]["cpp"]
        self.assertEqual(result["status"], "crashed")
        self.assertEqual(result["returncode"], -signal.SIGTERM)

    def test_timeout_preserves_partial_output_and_runs_other_collector(self):
        slow = self.runner("cpp", "import sys, time\nprint('partial', flush=True)\nprint('waiting', file=sys.stderr, flush=True)\ntime.sleep(10)")
        with self.assertRaises(RunnerFailure) as caught:
            self.compare(slow, self.output_runner("lean", EMPTY), timeout=0.5)
        report = self.report(caught.exception)
        self.assertEqual(report["runners"]["cpp"]["status"], "timeout")
        self.assertEqual(report["runners"]["lean"]["status"], "success")
        self.assertEqual((caught.exception.artifact_dir / "cpp.stdout").read_bytes(), b"partial\n")
        self.assertEqual((caught.exception.artifact_dir / "cpp.stderr").read_bytes(), b"waiting\n")

    def test_launch_failure_still_runs_other_collector(self):
        with self.assertRaises(RunnerFailure) as caught:
            self.compare(self.directory / "missing", self.output_runner("lean", EMPTY))
        report = self.report(caught.exception)
        self.assertEqual(report["runners"]["cpp"]["status"], "launch_error")
        self.assertEqual(report["runners"]["lean"]["status"], "success")

    def test_invalid_json_and_utf8_are_runner_failures(self):
        for output in [b"{broken", b"\xff", b"NaN", b"{\"x\": Infinity}"]:
            with self.subTest(output=output):
                bad = self.runner("cpp", f"import sys\nsys.stdout.buffer.write({output!r})")
                with self.assertRaises(RunnerFailure) as caught:
                    self.compare(bad, self.output_runner("lean", EMPTY))
                self.assertEqual(self.report(caught.exception)["runners"]["cpp"]["status"], "invalid_json")

    def test_stderr_on_success_is_a_runner_failure(self):
        body = f"import sys\nprint({json.dumps(EMPTY)!r})\nprint('unexpected', file=sys.stderr)"
        with self.assertRaises(RunnerFailure) as caught:
            self.compare(self.runner("cpp", body), self.output_runner("lean", EMPTY))
        self.assertEqual(self.report(caught.exception)["runners"]["cpp"]["status"], "unexpected_stderr")

    def test_invalid_api_configuration_does_not_launch(self):
        for timeout in [0, -1, float("inf"), float("nan")]:
            with self.assertRaises(ValueError):
                self.compare("missing", "missing", timeout=timeout)
        with self.assertRaises(ValueError):
            assert_equivalent(EMPTY, cpp="missing", lean="missing", algorithm="unknown")
        self.assertFalse(self.failure_dir.exists())

    def test_cli_exit_codes_and_exact_input_preservation(self):
        heap_file = self.directory / "input heap.json"
        original = b' {"format": "clover-gc-heap"}\n\n'
        heap_file.write_bytes(original)
        cpp = self.output_runner("cpp", EMPTY)
        args = ["--cpp", str(cpp), "--algorithm", "mark", "--collection", "full",
                "--failure-dir", str(self.failure_dir), str(heap_file)]
        cases = [(self.output_runner("same", EMPTY), 0),
                 (self.output_runner("different", {**EMPTY, "roots": [{"imm": 1}]}), 1),
                 (self.runner("reject", "raise SystemExit(1)"), 2)]
        for lean, expected in cases:
            with contextlib.redirect_stdout(io.StringIO()) as stdout, contextlib.redirect_stderr(io.StringIO()) as stderr:
                self.assertEqual(main(["--lean", str(lean), *args]), expected)
            self.assertEqual(bool(stdout.getvalue()), expected == 0)
            self.assertEqual(bool(stderr.getvalue()), expected != 0)
        for artifact in self.failure_dir.iterdir():
            self.assertEqual((artifact / "input.json").read_bytes(), original)


class RealRunnerTests(unittest.TestCase):
    def test_reviewed_fixture_with_both_algorithms(self):
        cpp = ROOT / "build-debug/tests/gc_differential/gc_cpp"
        lean = ROOT / "proofs/gc/.lake/build/bin/gc_lean"
        if not cpp.exists() or not lean.exists():
            self.skipTest("build gc_cpp and gc_lean to run the integration check")
        with tempfile.TemporaryDirectory(prefix="gc-compare-real-") as temp:
            heap = json.loads((FIXTURES / "before.json").read_bytes())
            for algorithm in ["mark", "evacuate"]:
                with self.subTest(algorithm=algorithm):
                    assert_equivalent(heap, cpp=cpp, lean=lean, algorithm=algorithm, failure_dir=temp)
            self.assertEqual(list(Path(temp).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
