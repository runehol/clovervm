def iter(obj):
    """iter(iterable) -> iterator
iter(callable, sentinel) -> iterator

Get an iterator from an object.  In the first form, the argument must
supply its own iterator, or be a sequence.
In the second form, the callable is called until it returns the sentinel."""
    return __clover_call_special__(
        obj, "__iter__", TypeError, "object is not iterable"
    )


def next(obj, *default):
    """next(iterator[, default])

Return the next item from the iterator. If default is given and the iterator
is exhausted, it is returned instead of raising StopIteration."""
    if len(default) > 1:
        raise TypeError
    try:
        return __clover_call_special__(
            obj, "__next__", TypeError, "object is not an iterator"
        )
    except StopIteration:
        if len(default) == 0:
            raise
        return default[0]


def repr(obj):
    """Return the canonical string representation of the object.

For many object types, including most builtins, eval(repr(obj)) == obj."""
    return __clover_call_special__(
        obj, "__repr__", TypeError, "object has no __repr__"
    )


def len(obj):
    """Return the number of items in a container."""
    return __clover_call_special__(
        obj, "__len__", TypeError, "object has no len()"
    )


def globals():
    """Return the dictionary containing the current scope's global variables."""
    return __clover_globals__()


def locals():
    """Return a dictionary containing the current scope's local variables."""
    return __clover_locals__()


def print(*args):
    """print(*args)

Print the values to standard output, separated by spaces and followed by a
newline."""
    first = True
    for obj in args:
        if first:
            first = False
        else:
            __clover_write_stdout__(" ")
        __clover_write_stdout__(
            __clover_call_special__(
                obj, "__str__", TypeError, "object has no __str__"
            )
        )
    __clover_write_stdout__("\n")


def sum(iterable, start=0):
    """Return the sum of a 'start' value (default: 0) plus an iterable of numbers.

When the iterable is empty, return the start value.
This function is intended specifically for use with numeric values and may
reject non-numeric types."""
    total = start
    for item in iterable:
        total = total + item
    return total


def any(iterable):
    """Return True if bool(x) is True for any x in the iterable.

If the iterable is empty, return False."""
    for item in iterable:
        if item:
            return True
    return False


def all(iterable):
    """Return True if bool(x) is True for all values x in the iterable.

If the iterable is empty, return True."""
    for item in iterable:
        if not item:
            return False
    return True


def min(*args):
    """With a single iterable argument, return its smallest item.

With two or more arguments, return the smallest argument."""
    if len(args) == 0:
        raise TypeError
    if len(args) == 1:
        iterable = args[0]
    else:
        iterable = args

    first = True
    for item in iterable:
        if first:
            best = item
            first = False
        elif item < best:
            best = item
    if first:
        raise ValueError
    return best


def max(*args):
    """With a single iterable argument, return its biggest item.

With two or more arguments, return the largest argument."""
    if len(args) == 0:
        raise TypeError
    if len(args) == 1:
        iterable = args[0]
    else:
        iterable = args

    first = True
    for item in iterable:
        if first:
            best = item
            first = False
        elif item > best:
            best = item
    if first:
        raise ValueError
    return best
