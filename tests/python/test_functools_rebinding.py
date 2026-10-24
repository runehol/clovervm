# CloverVM-specific surface and post-import builtin rebinding regression.
import builtins
import functools


assert functools.__all__ == ("reduce",)
assert functools.reduce.__doc__ is not None
assert not hasattr(functools, "partial")
assert not hasattr(functools, "wraps")
assert not hasattr(functools, "lru_cache")

# Once seeded, accumulator identity must never be reused as an omission flag.
def return_missing(a, b):
    return functools._missing


assert functools.reduce(return_missing, [1, 2, 3]) is functools._missing

old_iter = builtins.iter
old_next = builtins.next
old_len = builtins.len
old_object = builtins.object
old_type_error = builtins.TypeError
old_stop_iteration = builtins.StopIteration


def rebound(*args):
    raise AssertionError("rebound builtin called")


def add(a, b):
    return a + b


try:
    builtins.iter = rebound
    builtins.next = rebound
    builtins.len = rebound
    builtins.object = rebound
    builtins.TypeError = rebound
    builtins.StopIteration = rebound
    assert functools.reduce(add, [1, 2, 3]) == 6
    assert functools.reduce(None, [], initial=None) is None
    try:
        functools.reduce(None, [])
        assert False
    except old_type_error as caught:
        assert str(caught) == "reduce() of empty iterable with no initial value"
    try:
        functools.reduce(None, 42)
        assert False
    except old_type_error as caught:
        assert str(caught) == "reduce() arg 2 must support iteration"
finally:
    builtins.iter = old_iter
    builtins.next = old_next
    builtins.len = old_len
    builtins.object = old_object
    builtins.TypeError = old_type_error
    builtins.StopIteration = old_stop_iteration
