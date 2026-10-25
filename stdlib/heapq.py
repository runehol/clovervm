"""Heap operations and stable bounded selection.

This bounded implementation uses CPython's Python fallback algorithms and
ordinary Python list operations, including subclass hooks. Comparison failures
leave partial mutations in place; neither heap order nor preservation of every
original item is guaranteed after failure. Resizing from comparison callbacks
uses ordinary checked indexing rather than the C accelerator's size guards.
Length results support int, bool, and int subclasses within 64-bit size bounds;
non-integer results requiring custom __index__ dispatch remain unsupported.

Selection uses the existing iterable and callable protocol subset. General
custom __index__, sequence-only iteration, length hints, callable instances and
first-class bound-method callbacks remain unsupported. Bounded selection uses
SMI-sized range bounds; the n == 1 and known-size sorting shortcuts run first.
merge and native acceleration are not implemented.
"""

# Algorithms adapted from CPython's Lib/heapq.py at v3.14.5.
# Original code by Kevin O'Connor, augmented by Tim Peters and Raymond Hettinger.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

__all__ = ["heapify", "heappush", "heappop", "heapreplace", "heappushpop",
           "heapify_max", "heappush_max", "heappop_max", "heapreplace_max",
           "heappushpop_max", "nlargest", "nsmallest"]

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


def heapify_max(heap, /):
    """Transform list into a maxheap, in-place, in O(len(heap)) time."""
    _require_list(heap)
    n = _heap_len(heap)
    pos = n // 2 - 1
    while pos >= 0:
        _siftup_max(heap, pos)
        pos -= 1


def heappush_max(heap, item, /):
    """Maxheap version of a heappush."""
    _require_list(heap)
    heap.append(item)
    _siftdown_max(heap, 0, _heap_len(heap) - 1)


def heappop_max(heap, /):
    """Maxheap version of a heappop."""
    _require_list(heap)
    lastelt = heap.pop()    # raises appropriate IndexError if heap is empty
    if heap:
        returnitem = heap[0]
        heap[0] = lastelt
        _siftup_max(heap, 0)
        return returnitem
    return lastelt


def heapreplace_max(heap, item, /):
    """Maxheap version of a heappop followed by a heappush."""
    _require_list(heap)
    returnitem = heap[0]    # raises appropriate IndexError if heap is empty
    heap[0] = item
    _siftup_max(heap, 0)
    return returnitem


def heappushpop_max(heap, item, /):
    """Maxheap fast version of a heappush followed by a heappop."""
    _require_list(heap)
    if heap:
        if item < heap[0]:
            returnitem = heap[0]
            heap[0] = item
            _siftup_max(heap, 0)
            return returnitem
    return item


def _siftdown_max(heap, startpos, pos):
    'Maxheap variant of _siftdown'
    newitem = heap[pos]
    # Follow the path to the root, moving parents down until finding a place
    # newitem fits.
    while pos > startpos:
        parentpos = (pos - 1) >> 1
        parent = heap[parentpos]
        if parent < newitem:
            heap[pos] = parent
            pos = parentpos
            continue
        break
    heap[pos] = newitem


def _siftup_max(heap, pos):
    'Maxheap variant of _siftup'
    endpos = _heap_len(heap)
    startpos = pos
    newitem = heap[pos]
    # Bubble up the larger child until hitting a leaf.
    childpos = 2 * pos + 1    # leftmost child position
    while childpos < endpos:
        # Set childpos to index of larger child.
        rightpos = childpos + 1
        if rightpos < endpos and not heap[rightpos] < heap[childpos]:
            childpos = rightpos
        # Move the larger child up.
        heap[pos] = heap[childpos]
        pos = childpos
        childpos = 2 * pos + 1
    # The leaf at pos is empty now.  Put newitem there, and bubble it up
    # to its final resting place (by sifting its parents down).
    heap[pos] = newitem
    _siftdown_max(heap, startpos, pos)


def nlargest(n, iterable, key=None):
    """Find the n largest elements in a dataset.

    Equivalent to:  sorted(iterable, key=key, reverse=True)[:n]
    """

    # Short-cut for n==1 is to use max()
    if n == 1:
        it = iter(iterable)
        sentinel = object()
        result = max(it, default=sentinel, key=key)
        return [] if result is sentinel else [result]

    # range currently rejects bool bounds; normalize the builtin payload only.
    if isinstance(n, bool):
        n = int.__index__(n)

    # When n>=size, it's faster to use sorted()
    try:
        size = _heap_len(iterable)
    except TypeError:
        pass
    except AttributeError:
        pass
    else:
        if n >= size:
            return sorted(iterable, key=key, reverse=True)[:n]

    # When key is none, use simpler decoration
    if key is None:
        it = iter(iterable)
        result = []
        for pair in zip(range(0, -n, -1), it):
            i = pair[0]
            elem = pair[1]
            result.append((elem, i))
        if not result:
            return result
        heapify(result)
        top = result[0][0]
        order = -n
        _heapreplace = heapreplace
        for elem in it:
            if top < elem:
                _heapreplace(result, (elem, order))
                top = result[0][0]
                order -= 1
        result.sort(reverse=True)
        selected = []
        for entry in result:
            selected.append(entry[0])
        return selected

    # General case, slowest method
    it = iter(iterable)
    result = []
    for pair in zip(range(0, -n, -1), it):
        i = pair[0]
        elem = pair[1]
        result.append((key(elem), i, elem))
    if not result:
        return result
    heapify(result)
    top = result[0][0]
    order = -n
    _heapreplace = heapreplace
    for elem in it:
        k = key(elem)
        if top < k:
            _heapreplace(result, (k, order, elem))
            top = result[0][0]
            order -= 1
    result.sort(reverse=True)
    selected = []
    for entry in result:
        selected.append(entry[2])
    return selected


def nsmallest(n, iterable, key=None):
    """Find the n smallest elements in a dataset.

    Equivalent to:  sorted(iterable, key=key)[:n]
    """

    # Short-cut for n==1 is to use min()
    if n == 1:
        it = iter(iterable)
        sentinel = object()
        result = min(it, default=sentinel, key=key)
        return [] if result is sentinel else [result]

    # range currently rejects bool bounds; normalize the builtin payload only.
    if isinstance(n, bool):
        n = int.__index__(n)

    # When n>=size, it's faster to use sorted()
    try:
        size = _heap_len(iterable)
    except TypeError:
        pass
    except AttributeError:
        pass
    else:
        if n >= size:
            return sorted(iterable, key=key)[:n]

    # When key is none, use simpler decoration
    if key is None:
        it = iter(iterable)
        # put the range(n) first so that zip() doesn't
        # consume one too many elements from the iterator
        result = []
        for pair in zip(range(n), it):
            i = pair[0]
            elem = pair[1]
            result.append((elem, i))
        if not result:
            return result
        heapify_max(result)
        top = result[0][0]
        order = n
        _heapreplace = heapreplace_max
        for elem in it:
            if elem < top:
                _heapreplace(result, (elem, order))
                top = result[0][0]
                order += 1
        result.sort()
        selected = []
        for entry in result:
            selected.append(entry[0])
        return selected

    # General case, slowest method
    it = iter(iterable)
    result = []
    for pair in zip(range(n), it):
        i = pair[0]
        elem = pair[1]
        result.append((key(elem), i, elem))
    if not result:
        return result
    heapify_max(result)
    top = result[0][0]
    order = n
    _heapreplace = heapreplace_max
    for elem in it:
        k = key(elem)
        if k < top:
            _heapreplace(result, (k, order, elem))
            top = result[0][0]
            order += 1
    result.sort()
    selected = []
    for entry in result:
        selected.append(entry[2])
    return selected
