"""Run production heapq regressions and compare CPython 3.14.5 fallback traces.

Run explicitly: python3.14 tests/host/test_heapq_expansion.py
CPython algorithms are loaded from the matching installed Lib/heapq.py with
_heapq imports blocked, so subclass hooks and mutation failures use the intended
Python fallback oracle rather than accelerator behavior.
"""
import builtins
import heapq
import importlib.util
from pathlib import Path
import random
import runpy
import sys
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def production_module():
    spec = importlib.util.spec_from_file_location(
        "clover_heapq", ROOT / "stdlib" / "heapq.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def reference_module():
    def without_accelerator(name, *args, **kwargs):
        if name == "_heapq":
            raise ImportError("force Python fallback")
        return builtins.__import__(name, *args, **kwargs)

    module = types.ModuleType("reference_heapq")
    module.__dict__["__builtins__"] = dict(
        vars(builtins), __import__=without_accelerator)
    path = Path(heapq.__file__)
    exec(compile(path.read_text(), str(path), "exec"), module.__dict__)
    assert hasattr(module.heapify_max, "__code__")
    return module


class ExpansionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if sys.implementation.name != "cpython" or sys.version_info[:3] != (3, 14, 5):
            raise RuntimeError("reference tests require CPython 3.14.5")
        cls.production = production_module()
        cls.reference = reference_module()

    def test_vm_regressions_on_production_source(self):
        with patch.dict(sys.modules, heapq=self.production):
            for name in ("heapq_min", "heapq_max", "heapq_selection"):
                with self.subTest(name=name):
                    runpy.run_path(str(ROOT / "tests" / "python" / (name + ".py")))

    def test_selection_oracles(self):
        rng = random.Random(1701)
        for size in range(70):
            data = [(rng.randrange(15), i) for i in range(size)]
            for name in ("nlargest", "nsmallest"):
                for n in (-2, 0, 1, 2, 7, 69, 80):
                    for keyed in (False, True):
                        results = []
                        for module in (self.reference, self.production):
                            calls = []
                            def key(row):
                                calls.append(row)
                                return row[0]
                            result = getattr(module, name)(
                                n, iter(data), key=key if keyed else None)
                            results.append((result, calls))
                        self.assertEqual(*results)

    def test_comparison_and_hook_traces(self):
        for name in ("heapify_max", "heappush_max", "heappop_max",
                     "heapreplace_max", "heappushpop_max"):
            for action in ("normal", "raise", "clear"):
                results = []
                for module in (self.reference, self.production):
                    log = []
                    failure = ValueError("comparison failure")
                    class Item:
                        def __init__(self, value):
                            self.value = value
                        def __lt__(self, other):
                            log.append(("lt", self.value, other.value))
                            if action == "raise":
                                raise failure
                            if action == "clear":
                                heap.clear()
                            return self.value < other.value
                    class Heap(list):
                        def __len__(self):
                            log.append("len")
                            return super().__len__()
                        def __getitem__(self, index):
                            log.append(("get", index))
                            return super().__getitem__(index)
                        def __setitem__(self, index, value):
                            log.append(("set", index, value.value))
                            return super().__setitem__(index, value)
                        def append(self, value):
                            log.append(("append", value.value))
                            return super().append(value)
                        def pop(self):
                            log.append("pop")
                            return super().pop()
                    heap = Heap([Item(5), Item(3), Item(2), Item(1)])
                    args = (heap,) if name in ("heapify_max", "heappop_max") else (heap, Item(4))
                    try:
                        result = getattr(module, name)(*args)
                        outcome = None if result is None else result.value
                    except Exception as error:
                        outcome = (type(error).__name__, error is failure)
                    results.append((outcome, [item.value for item in heap], log))
                with self.subTest(name=name, action=action):
                    self.assertEqual(*results)

    def test_selection_callback_traces(self):
        for name in ("nlargest", "nsmallest"):
            for n in (-1, 0, 1, 2, 6):
                for sized in (False, True):
                    results = []
                    for module in (self.reference, self.production):
                        log = []
                        class Source:
                            def __init__(self):
                                self.values = iter([3, 1, 4, 1, 5])
                            def __len__(self):
                                log.append("len")
                                if not sized:
                                    raise TypeError
                                return 5
                            def __iter__(self):
                                log.append("iter")
                                return self
                            def __next__(self):
                                log.append("next")
                                return next(self.values)
                        def key(value):
                            log.append(("key", value))
                            return value
                        result = getattr(module, name)(n, Source(), key)
                        results.append((result, log))
                    self.assertEqual(*results)


if __name__ == "__main__":
    unittest.main()
