def iter(obj):
    return __clover_call_special__(obj, "__iter__", TypeError, "object is not iterable")


def next(obj):
    return __clover_call_special__(obj, "__next__", TypeError, "object is not an iterator")


def repr(obj):
    return __clover_call_special__(obj, "__repr__", TypeError, "object has no __repr__")
