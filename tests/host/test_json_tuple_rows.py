"""Production JSON row-policy tests on CPython 3.14.5.

Run explicitly: python3.14 tests/host/test_json_tuple_rows.py
Tuple-subclass allocation is not yet supported by CloverVM. These tests execute
its unmodified stdlib/json.py source with real CPython tuple subclasses; they do
not claim VM subclass coverage. The import adapter implements only exact-dict
identity and rejects cursor access, which these dict-subclass cases never need.
Original tests, with behavior checked against CPython v3.14.5 Modules/_json.c.
"""
import builtins
import json
from pathlib import Path
import sys
import types
import unittest
import _json


def load_production_json():
    def reject_cursor(*args):
        raise AssertionError("tuple-row tests must not enter the native cursor")

    bridge = types.SimpleNamespace(
        is_exact_dict=lambda obj: type(obj) is dict,
        dict_next=reject_cursor,
    )

    def import_bridge(name, globals=None, locals=None, fromlist=(), level=0):
        if name == "_json" and level == 0:
            return bridge
        return builtins.__import__(name, globals, locals, fromlist, level)

    source = Path(__file__).resolve().parents[2] / "stdlib" / "json.py"
    module = types.ModuleType("clover_json_under_test")
    module.__dict__["__builtins__"] = dict(vars(builtins), __import__=import_bridge)
    exec(compile(source.read_text(), str(source), "exec"), module.__dict__)
    return module


class RaisingRow(tuple):
    def __len__(self):
        raise AssertionError("overridden row length")

    def __getitem__(self, index):
        raise AssertionError("overridden row indexing")


class MisleadingRow(tuple):
    def __len__(self):
        return 2

    def __getitem__(self, index):
        return ("wrong", 99)[index]


class MisleadingLengthRow(tuple):
    def __len__(self):
        return 0


class Mapping(dict):
    def __init__(self, rows):
        dict.__init__(self, real=1)  # Empty dicts bypass items().
        self.rows = rows

    def items(self):
        return self.rows


class TupleRowsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if (sys.implementation.name != "cpython"
                or sys.version_info[:3] != (3, 14, 5)):
            raise RuntimeError("reference tests require CPython 3.14.5")
        if json.encoder.c_make_encoder is not _json.make_encoder:
            raise RuntimeError("CPython JSON accelerator must be enabled")
        cls.production = load_production_json()

    def test_stored_payload_and_length(self):
        for row_type in (RaisingRow, MisleadingRow, MisleadingLengthRow):
            for sorting in (False, True):
                for encoder in (json, self.production):
                    with self.subTest(row=row_type, sorting=sorting, encoder=encoder):
                        rows = [row_type(("z", 1)), row_type(("a", 2))]
                        expected = ('{"a": 2, "z": 1}' if sorting
                                    else '{"z": 1, "a": 2}')
                        self.assertEqual(
                            encoder.dumps(Mapping(rows), sort_keys=sorting), expected)
                        self.assertEqual(
                            tuple.__getitem__(rows[0], 0), "a" if sorting else "z")

    def test_malformed_stored_lengths(self):
        for row_type in (RaisingRow, MisleadingRow):
            for payload in ((), ("a",), ("a", 1, 2)):
                for sorting in (False, True):
                    for encoder in (json, self.production):
                        with self.subTest(row=row_type, payload=payload, sorting=sorting,
                                          encoder=encoder):
                            with self.assertRaisesRegex(
                                    ValueError, "^items must return 2-tuples$"):
                                encoder.dumps(Mapping([row_type(payload)]), sort_keys=sorting)

    def test_shared_rows_and_callback_before_later_validation(self):
        class Unsupported:
            pass

        for sorting in (False, True):
            for encoder in (json, self.production):
                calls = []
                rows = [RaisingRow(("a", Unsupported())), RaisingRow(("z",))]

                def callback(value):
                    calls.append("default")
                    return 1

                with self.assertRaisesRegex(ValueError, "^items must return 2-tuples$"):
                    encoder.dumps(Mapping(rows), default=callback, sort_keys=sorting)
                self.assertEqual(calls, ["default"])

                rows = [RaisingRow(("a", Unsupported())), RaisingRow(("b", 2))]

                def append_row(value):
                    rows.append(RaisingRow(("c", 3)))
                    return 1

                self.assertEqual(
                    encoder.dumps(Mapping(rows), default=append_row,
                                  sort_keys=sorting),
                    '{"a": 1, "b": 2, "c": 3}')
                self.assertEqual(len(rows), 3)

    def test_sort_exception_precedes_row_validation(self):
        class ComparisonError(Exception):
            pass

        class SortRow(RaisingRow):
            def __lt__(self, other):
                raise ComparisonError("sorting first")

        for encoder in (json, self.production):
            with self.assertRaisesRegex(ComparisonError, "sorting first"):
                encoder.dumps(Mapping([SortRow(()), SortRow(())]), sort_keys=True)


if __name__ == "__main__":
    unittest.main()
