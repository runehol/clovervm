"""Function tools supported by CloverVM: streaming reduce only.

Callbacks use the VM's existing function and class call support; general
callable instances and bound methods remain unsupported. Iterables use the
existing iter protocol. Other functools APIs are not implemented.
Private builtin aliases capture bindings at import time, not before import.
"""

from builtins import iter as _iter
from builtins import object as _object
from builtins import TypeError as _TypeError
from builtins import StopIteration as _StopIteration


__all__ = ("reduce",)
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
