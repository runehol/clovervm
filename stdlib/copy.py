"""Bounded shallow copying and field replacement.

Reduction-based reconstruction and deepcopy are intentionally unsupported.
"""
# Dispatch structure and replace adapted from CPython Lib/copy.py, v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

from builtins import __clover_runtime_type__ as _runtime_type


class Error(Exception):
    pass


error = Error
__all__ = ["Error", "copy", "replace"]

_atomic_types = (
    _runtime_type(None), bool, int, float, str, bytes, tuple, slice,
    _runtime_type(Ellipsis), _runtime_type(NotImplemented),
)


def copy(x):
    """Return a shallow copy of a supported builtin or call its class hook."""
    cls = _runtime_type(x)
    if cls in _atomic_types:
        return x
    if cls is list:
        return list.copy(x)
    if cls is dict:
        return dict.copy(x)
    if issubclass(cls, type):
        return x
    copier = getattr(cls, "__copy__", None)
    if copier is not None:
        return copier(x)
    raise Error("un(shallow)copyable object of type " + cls.__name__)


def replace(obj, /, **changes):
    """Call the class replacement hook, forwarding all field changes."""
    cls = obj.__class__
    func = getattr(cls, "__replace__", None)
    if func is None:
        raise TypeError("replace() does not support " + cls.__name__ + " objects")
    return func(obj, **changes)
