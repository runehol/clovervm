def iter(obj):
    """iter(iterable) -> iterator
iter(callable, sentinel) -> iterator

Get an iterator from an object.  In the first form, the argument must
supply its own iterator, or be a sequence.
In the second form, the callable is called until it returns the sentinel."""
    return __clover_call_special__(
        obj, "__iter__", TypeError, "object is not iterable"
    )


def next(obj):
    """next(iterator[, default])

Return the next item from the iterator. If default is given and the iterator
is exhausted, it is returned instead of raising StopIteration."""
    return __clover_call_special__(
        obj, "__next__", TypeError, "object is not an iterator"
    )


def repr(obj):
    """Return the canonical string representation of the object.

For many object types, including most builtins, eval(repr(obj)) == obj."""
    return __clover_call_special__(
        obj, "__repr__", TypeError, "object has no __repr__"
    )
