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


# Identity-based recursion tracking for the current single-threaded runtime.
# Keep this outside instance storage so it cannot leak into namespace attributes.
_namespace_repr_running = []


class SimpleNamespace:
    """A simple attribute-based namespace."""

    def __repr__(self, /):
        name = self.__class__.__name__
        if self.__class__ is SimpleNamespace:
            name = "namespace"
        for active in _namespace_repr_running:
            if active is self:
                return name + "(...)"
        _namespace_repr_running.append(self)
        try:
            attributes = self.__dict__
            pairs = []
            for key in attributes.keys():
                if str.__len__(key):
                    try:
                        value = attributes[key]
                    except KeyError:
                        continue
                    pairs.append("".join([key, "=", repr(value)]))
            return name + "(" + ", ".join(pairs) + ")"
        finally:
            _namespace_repr_running.pop()


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
