"""Bisection and sorted insertion using CloverVM's existing sequence protocols.

Bounds accept builtin integers, bools, and integer subclasses through
operator.index; general __index__ conversion is deferred. Keys use the VM's
existing callable support. Unlike CPython's accelerator, an empty search does
not eagerly validate sequence slots, and mappings are not rejected separately.
Comparisons use normal Python dispatch rather than the accelerator's repeated
same-type __lt__ call when its first result is NotImplemented.
"""

# Search loops adapted from CPython's Lib/bisect.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

from operator import index as _index


__all__ = (
    "bisect", "bisect_left", "bisect_right", "insort", "insort_left", "insort_right"
)

_SSIZE_MIN = -(2**63)
_SSIZE_MAX = 2**63 - 1


def _bound(value):
    value = _index(value)
    if value < _SSIZE_MIN or value > _SSIZE_MAX:
        raise OverflowError("Python int too large to convert to C ssize_t")
    return value


def _upper(a, lo, hi):
    if lo < 0:
        raise ValueError("lo must be non-negative")
    if hi is None or hi == -1:
        # len() currently returns the special method's result unchecked.
        hi = _index(len(a))
        if hi < 0:
            raise ValueError("__len__() should return >= 0")
        if hi > _SSIZE_MAX:
            raise OverflowError("cannot fit length into an index-sized integer")
    return hi


def _left(a, x, lo, hi, key):
    hi = _upper(a, lo, hi)
    while lo < hi:
        mid = (lo + hi) // 2
        item = a[mid]
        if key is not None:
            item = key(item)
        if item < x:
            lo = mid + 1
        else:
            hi = mid
    return lo


def _right(a, x, lo, hi, key):
    hi = _upper(a, lo, hi)
    while lo < hi:
        mid = (lo + hi) // 2
        item = a[mid]
        if key is not None:
            item = key(item)
        if x < item:
            hi = mid
        else:
            lo = mid + 1
    return lo


def bisect_left(a, x, lo=0, hi=None, *, key=None):
    """Return the insertion index before existing equal items in a[lo:hi]."""
    lo = _bound(lo)
    if hi is not None:
        hi = _bound(hi)
    return _left(a, x, lo, hi, key)


def bisect_right(a, x, lo=0, hi=None, *, key=None):
    """Return the insertion index after existing equal items in a[lo:hi]."""
    lo = _bound(lo)
    if hi is not None:
        hi = _bound(hi)
    return _right(a, x, lo, hi, key)


def insort_left(a, x, lo=0, hi=None, *, key=None):
    """Insert x before existing equal items, searching a[lo:hi]."""
    lo = _bound(lo)
    if hi is not None:
        hi = _bound(hi)
    search = x
    if key is not None:
        search = key(x)
    position = _left(a, search, lo, hi, key)
    a.insert(position, x)


def insort_right(a, x, lo=0, hi=None, *, key=None):
    """Insert x after existing equal items, searching a[lo:hi]."""
    lo = _bound(lo)
    if hi is not None:
        hi = _bound(hi)
    search = x
    if key is not None:
        search = key(x)
    position = _right(a, search, lo, hi, key)
    a.insert(position, x)


bisect = bisect_right
insort = insort_right
