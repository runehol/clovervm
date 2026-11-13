"""Campaign checks; use the isolated host environment in requirements.txt."""

import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

try:
    import hypothesis
except ModuleNotFoundError as error:
    if error.name == "hypothesis":
        raise unittest.SkipTest("install tests/gc_differential/requirements.txt for campaign tests")
    raise

from campaign import main, run_campaign
from compare import HeapMismatch, RunnerFailure
from hypothesis.errors import FailedHealthCheck


ROOT = Path(__file__).resolve().parents[2]


class CampaignTests(unittest.TestCase):
    def test_seed_replays_separate_properties_with_requested_bounds(self):
        def collect():
            seen = {"mark": [], "evacuate": []}

            def check(heap, **options):
                self.assertLessEqual(len(heap["objects"]), 4)
                seen[options["algorithm"]].append(json.dumps(heap, sort_keys=True))

            with patch("campaign.assert_equivalent", side_effect=check):
                counts = run_campaign(cpp="unused", lean="unused", seed=132,
                                      examples=25, max_objects=4)
            self.assertEqual(counts, {name: len(heaps) for name, heaps in seen.items()})
            self.assertTrue(all(seen.values()))
            return seen

        self.assertEqual(collect(), collect())

    def test_algorithm_selection_does_not_run_the_other_property(self):
        with patch("campaign.assert_equivalent") as check:
            counts = run_campaign(cpp="unused", lean="unused", algorithm="evacuate", collection="young",
                                  seed=1, examples=5)
        self.assertEqual(list(counts), ["evacuate"])
        self.assertEqual(check.call_count, counts["evacuate"])
        self.assertTrue(all(call.kwargs["algorithm"] == "evacuate" for call in check.call_args_list))
        self.assertTrue(all(call.kwargs["collection"] == "young" for call in check.call_args_list))
        for call in check.call_args_list:
            heap = call.args[0]
            objects = {obj["allocation_id"]: obj for obj in heap["objects"]}
            for owner in objects.values():
                if owner["state"]["kind"] == "young_original":
                    continue
                refs = [cell.get("ref") for cell in owner["strong"]]
                for unit in owner["weak"]:
                    refs += unit.get("guards", []) + [unit.get("payload", unit.get("value", {})).get("ref")]
                if any(ref is not None and objects[ref]["state"]["kind"] == "young_original" for ref in refs):
                    self.assertIn(owner["allocation_id"], heap["remembered"])

    def test_invalid_configuration_does_not_run_collectors(self):
        with patch("campaign.assert_equivalent") as check:
            for options in [{"examples": 0}, {"max_objects": -1}, {"algorithm": "unknown"},
                            {"timeout": 0}, {"timeout": float("nan")},
                            {"collection": "young"}, {"collection": "unknown"}]:
                with self.subTest(options=options), self.assertRaises(ValueError):
                    run_campaign(cpp="unused", lean="unused", **options)
            check.assert_not_called()

    def test_framework_failure_is_not_reported_as_heap_mismatch(self):
        with patch("campaign.run_campaign", side_effect=FailedHealthCheck("injected health check")):
            with contextlib.redirect_stderr(io.StringIO()) as stderr:
                code = main(["--cpp", "unused", "--lean", "unused", "--collection", "full"])
        self.assertEqual(code, 2)
        self.assertIn("injected health check", stderr.getvalue())

    def test_database_replays_failure_before_new_generation(self):
        with tempfile.TemporaryDirectory(prefix="gc-campaign-replay-") as temp:
            directory = Path(temp)
            prefix = f"#!{sys.executable}\nimport json, sys\nh = json.load(open(sys.argv[-1]))\n"
            cpp = directory / "cpp"
            cpp.write_text(prefix + "print(json.dumps(h))\n")
            lean = directory / "lean"
            lean.write_text(prefix + "if h['external_weak'] or any(o['weak'] for o in h['objects']): h['roots'].append({'imm': 7})\nprint(json.dumps(h))\n")
            for runner in [cpp, lean]:
                runner.chmod(0o700)
            # Separate processes verify persisted reuse, rather than Hypothesis's
            # in-process cache. An explicit profile also works under CI defaults.
            code = f"""
import sys
sys.path.insert(0, {str(Path(__file__).resolve().parent)!r})
from hypothesis import settings
from hypothesis.database import DirectoryBasedExampleDatabase
from campaign import run_campaign
from compare import HeapMismatch
settings.register_profile('replay-test', derandomize=False,
    database=DirectoryBasedExampleDatabase(sys.argv[2]))
settings.load_profile('replay-test')
try:
    run_campaign(cpp={str(cpp)!r}, lean={str(lean)!r}, algorithm='evacuate', collection=sys.argv[3],
        max_objects=3, examples=int(sys.argv[1]), failure_dir={str(directory / 'failures')!r})
except HeapMismatch:
    sys.exit(7)
"""
            for budget, database, collection, expected in [(1, "control", "full", 0), (100, "database", "full", 7),
                                                           (1, "database", "young", 0), (1, "database", "full", 7)]:
                result = subprocess.run([sys.executable, "-c", code, str(budget), str(directory / database), collection],
                                        cwd=directory, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_injected_failures_keep_inputs_and_campaign_metadata(self):
        with tempfile.TemporaryDirectory(prefix="gc-campaign-test-") as temp:
            directory = Path(temp)

            def runner(name, body):
                path = directory / name
                path.write_text(f"#!{sys.executable}\n{body}\n", encoding="utf-8")
                path.chmod(0o700)
                return path

            copy = "import json, sys\nh = json.load(open(sys.argv[-1]))\n"
            cpp = runner("cpp", copy + "print(json.dumps(h))")
            mismatch = runner("mismatch", copy + "h['roots'].append({'imm': 7})\nprint(json.dumps(h))")
            rejected = runner("rejected", "raise SystemExit(1)")
            for lean, expected_code, expected_type, outcome in [
                    (mismatch, 1, HeapMismatch, "mismatch"),
                    (rejected, 2, RunnerFailure, "runner_failure")]:
                with self.subTest(outcome=outcome):
                    failures = directory / ("failures-" + outcome)
                    with self.assertRaises(expected_type):
                        run_campaign(cpp=cpp, lean=lean, algorithm="mark", seed=132,
                                     examples=10, max_objects=3, failure_dir=failures)
                    original = {p: p.read_bytes() for p in failures.glob("*/input.json")}
                    self.assertTrue(original)
                    for input_file in original:
                        heap = json.loads(input_file.read_bytes())
                        self.assertLessEqual(len(heap["objects"]), 3)
                        report = json.loads(input_file.with_name("run.json").read_text())
                        self.assertEqual(report["outcome"], outcome)
                        self.assertEqual(report["algorithm"], "mark")
                        self.assertEqual(report["campaign"]["seed"], 132)
                        self.assertEqual(report["campaign"]["max_objects"], 3)
                        self.assertEqual(report["campaign"]["phases"], ["reuse", "generate"])
                        self.assertEqual(report["campaign"]["hypothesis_version"], hypothesis.__version__)
                    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as stderr:
                        code = main(["--cpp", str(cpp), "--lean", str(lean), "--algorithm", "mark",
                                     "--collection", "full", "--seed", "132", "--examples", "10",
                                     "--max-objects", "3", "--failure-dir", str(failures)])
                    self.assertEqual(code, expected_code)
                    self.assertIn("reproduction:", stderr.getvalue())
                    self.assertEqual({p: p.read_bytes() for p in original}, original)


class RealCampaignTests(unittest.TestCase):
    def test_generated_heaps_accepted_by_both_runners(self):
        cpp = ROOT / "build-debug/tests/gc_differential/gc_cpp"
        lean = ROOT / "proofs/gc/.lake/build/bin/gc_lean"
        if not cpp.exists() or not lean.exists():
            self.skipTest("build gc_cpp and gc_lean to run generated-heap checks")
        with tempfile.TemporaryDirectory(prefix="gc-campaign-real-") as temp:
            for collection in ["full", "young"]:
                for bound in [0, 12]:
                    with self.subTest(max_objects=bound, collection=collection):
                        counts = run_campaign(cpp=cpp, lean=lean, examples=20, max_objects=bound, collection=collection,
                                              algorithm="both" if collection == "full" else "evacuate",
                                              seed=132, failure_dir=temp)
                        self.assertTrue(all(count > 0 for count in counts.values()))
            self.assertEqual(list(Path(temp).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
