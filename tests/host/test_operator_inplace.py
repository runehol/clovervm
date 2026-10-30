"""Compare native CPython and the production managed arithmetic wrappers."""
import importlib.util
import operator
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
source_path = ROOT / "tests/python/test_operator_inplace.py"
source = compile(source_path.read_text(), str(source_path), "exec")
exec(source, {})

spec = importlib.util.spec_from_file_location("operator", ROOT / "stdlib/operator.py")
managed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(managed)
try:
    sys.modules["operator"] = managed
    exec(source, {})
    # Retain the existing managed keyword signatures deliberately.
    assert managed.iadd(a=2, b=3) == 5
    assert managed.__imul__(a=3, b=4) == 12
finally:
    sys.modules["operator"] = operator
print("CPython and managed operator in-place regressions passed")
