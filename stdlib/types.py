"""Define names for built-in types that are useful to import explicitly.

This is an intentionally small CloverVM subset of CPython's ``types`` module.
It exposes runtime classes that already exist and are safely reachable from
Python code.
"""

import sys


def _function_type_probe():
    pass


FunctionType = _function_type_probe.__class__
LambdaType = FunctionType


CodeType = _function_type_probe.__code__.__class__
BuiltinFunctionType = FunctionType
BuiltinMethodType = FunctionType
MethodType = FunctionType
ModuleType = sys.__class__
NoneType = None.__class__


class SimpleNamespace:
    """A simple attribute-based namespace."""

    def __repr__(self):
        return "namespace(...)"


def get_original_bases(cls):
    """Return a class's original bases, falling back to __bases__."""
    return getattr(cls, "__orig_bases__", cls.__bases__)


__all__ = [
    "BuiltinFunctionType",
    "BuiltinMethodType",
    "CodeType",
    "FunctionType",
    "LambdaType",
    "MethodType",
    "ModuleType",
    "NoneType",
    "SimpleNamespace",
    "get_original_bases",
]
