"""Min-heap operations on lists and list subclasses.

This bounded implementation uses CPython's Python fallback algorithms and
ordinary Python list operations, including subclass hooks. Comparison failures
leave partial mutations in place; neither heap order nor preservation of every
original item is guaranteed after failure. Resizing from comparison callbacks
uses ordinary checked indexing rather than the C accelerator's size guards.
Length results support int, bool, and int subclasses within 64-bit size bounds;
non-integer results requiring custom __index__ dispatch remain unsupported.

merge, nlargest, nsmallest, and max-heap operations are not implemented.
"""

# Algorithms adapted from CPython's Lib/heapq.py at v3.14.5.
# Original code by Kevin O'Connor, augmented by Tim Peters and Raymond Hettinger.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

__all__ = ["heapify", "heappush", "heappop", "heapreplace", "heappushpop"]

# The VM's current targets use 64-bit platform sizes. As in operator.py, avoid
# sys.maxsize here because it currently exposes the narrower SMI limit.
_SSIZE_MAX = 2**63 - 1


def _require_list(heap):
    if not isinstance(heap, list):
        raise TypeError("heap must be a list")


def _heap_len(heap):
    # The current builtin len returns the raw __len__ result. Normalize its
    # integer payload before validation or arithmetic, bypassing subclass hooks.
    length = len(heap)
    if not isinstance(length, int):
        raise TypeError("__len__() should return an integer")
    length = int.__index__(length)
    if length < 0:
        raise ValueError("__len__() should return >= 0")
    if length > _SSIZE_MAX:
        raise OverflowError("Python int too large to convert to C ssize_t")
    return length


def heapify(heap, /):
    """Transform heap in place into a min-heap in linear time."""
    _require_list(heap)
    pos = _heap_len(heap) // 2 - 1
    while pos >= 0:
        _siftup(heap, pos)
        pos -= 1


def heappush(heap, item, /):
    """Push item onto heap, maintaining the heap invariant."""
    _require_list(heap)
    heap.append(item)
    _siftdown(heap, 0, _heap_len(heap) - 1)


def heappop(heap, /):
    """Remove and return the smallest item, raising IndexError if empty."""
    _require_list(heap)
    lastelt = heap.pop()
    if heap:
        returnitem = heap[0]
        heap[0] = lastelt
        _siftup(heap, 0)
        return returnitem
    return lastelt


def heapreplace(heap, item, /):
    """Replace and return the smallest item, raising IndexError if empty."""
    _require_list(heap)
    returnitem = heap[0]
    heap[0] = item
    _siftup(heap, 0)
    return returnitem


def heappushpop(heap, item, /):
    """Push item and return the smallest item, keeping heap's size unchanged."""
    _require_list(heap)
    # Separate conditions avoid repeated truth conversion of a false heap in
    # the VM's current lowering of an `and` expression used as a condition.
    if heap:
        if heap[0] < item:
            returnitem = heap[0]
            heap[0] = item
            _siftup(heap, 0)
            return returnitem
    return item


def _siftdown(heap, startpos, pos):
    newitem = heap[pos]
    while pos > startpos:
        parentpos = (pos - 1) >> 1
        parent = heap[parentpos]
        if newitem < parent:
            heap[pos] = parent
            pos = parentpos
            continue
        break
    heap[pos] = newitem


def _siftup(heap, pos):
    endpos = _heap_len(heap)
    startpos = pos
    newitem = heap[pos]
    childpos = 2 * pos + 1
    while childpos < endpos:
        rightpos = childpos + 1
        if rightpos < endpos and not heap[childpos] < heap[rightpos]:
            childpos = rightpos
        heap[pos] = heap[childpos]
        pos = childpos
        childpos = 2 * pos + 1
    heap[pos] = newitem
    _siftdown(heap, startpos, pos)
