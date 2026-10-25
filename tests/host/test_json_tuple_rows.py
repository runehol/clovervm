"""Production JSON row-policy tests on CPython 3.14.5.

Run explicitly: python3.14 tests/host/test_json_tuple_rows.py
The ordinary VM harness executes the same row regressions. This host wrapper
also checks the unmodified production stdlib/json.py source against accelerated
CPython dumps. The import adapter implements only exact-dict identity and
rejects cursor access, which these dict-subclass cases never need.
Original tests, with behavior checked against CPython v3.14.5 Modules/_json.c.
"""
import builtins
import json
from pathlib import Path
import runpy
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


class TupleRowsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if (sys.implementation.name != "cpython"
                or sys.version_info[:3] != (3, 14, 5)):
            raise RuntimeError("reference tests require CPython 3.14.5")
        if json.encoder.c_make_encoder is not _json.make_encoder:
            raise RuntimeError("CPython JSON accelerator must be enabled")
        cls.production = load_production_json()

    def test_shared_vm_regressions(self):
        source = (Path(__file__).resolve().parents[1]
                  / "python" / "test_json_tuple_rows.py")
        # Executing the self-checking VM source checks accelerated CPython first;
        # then run its identical assertions against the production-source adapter.
        checks = None
        with self.subTest(encoder="CPython accelerator"):
            checks = runpy.run_path(str(source))
        if checks is None:
            return  # Preserve the accelerator failure without a secondary error.
        with self.subTest(encoder="production source"):
            checks["check_tuple_rows"](self.production)


if __name__ == "__main__":
    unittest.main()
