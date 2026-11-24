"""Streaming reduction and comparison-to-key adaptation.

Callbacks and comparisons use the VM's existing dispatch protocols.
Private builtin aliases capture bindings at import time.
"""

from builtins import __clover_runtime_type__ as _runtime_type
from builtins import iter as _iter
from builtins import object as _object
from builtins import AttributeError as _AttributeError
from builtins import TypeError as _TypeError
from builtins import StopIteration as _StopIteration


__all__ = ("reduce", "cmp_to_key")
_missing = _object()


def reduce(function, iterable, /, initial=_missing):
    """Fold an iterable from left to right, optionally starting with initial."""
    acquisition_failed = False
    try:
        iterator = _iter(iterable)
    except _TypeError:
        acquisition_failed = True
    if acquisition_failed:
        # Raise outside the handler so the replaced exception is not context.
        raise _TypeError("reduce() arg 2 must support iteration")

    value = initial
    have_value = initial is not _missing
    while True:
        try:
            # Use the existing primitive directly: bootstrap next() consults
            # live builtins.len even when no default argument is supplied.
            item = __clover_call_special__(
                iterator, "__next__", TypeError, "object is not an iterator"
            )
        except _StopIteration:
            break

        if have_value:
            value = function(value, item)
        else:
            value = item
            have_value = True

    if not have_value:
        raise _TypeError("reduce() of empty iterable with no initial value")
    return value


class _KeyWrapper:
    __hash__ = None

    def __init__(self, comparator, wrapped):
        self._comparator = comparator
        self._wrapped = wrapped

    def __call__(self, /, obj):
        return _KeyWrapper(self._comparator, (obj,))

    def _get_obj(self):
        if self._wrapped:
            return self._wrapped[0]
        return None

    def _set_obj(self, obj):
        self._wrapped = (obj,)

    def _del_obj(self):
        self._wrapped = ()

    # An empty tuple represents an unset obj. A one-item tuple can wrap None.
    obj = property(_get_obj, _set_obj, _del_obj)

    def _compare_with(self, other):
        if _runtime_type(other) is not _KeyWrapper:
            raise _TypeError("other argument must be K instance")
        left = self._wrapped
        right = other._wrapped
        if not left or not right:
            raise _AttributeError("object")
        return self._comparator(left[0], right[0])

    def __lt__(self, other, /):
        return self._compare_with(other) < 0

    def __le__(self, other, /):
        return self._compare_with(other) <= 0

    def __eq__(self, other, /):
        return self._compare_with(other) == 0

    def __ne__(self, other, /):
        return self._compare_with(other) != 0

    def __gt__(self, other, /):
        return self._compare_with(other) > 0

    def __ge__(self, other, /):
        return self._compare_with(other) >= 0


class _CmpToKey:
    """Convert a two-argument comparator into a key callable."""

    def __call__(self, /, mycmp):
        return _KeyWrapper(mycmp, ())


# A callable instance keeps the same call signature through class attributes.
cmp_to_key = _CmpToKey()
