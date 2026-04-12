# Refcounting and Safepoints

We want to have a GIL-less implementation. However, we still want Python C extension compatibility, so we need to do something here.

We prefer to do deferred reference counting. This means that we don't refcount values on the stack, only on the heap. Loading values from the heap to a local variable, as well as moving values between local variables and temporaries is therefore a matter of a `MOV`, whereas we only need to adjust refcounts when storing to a heap, such as when assigning to a dictionary, object, list or similar. When we do this, we `INCREF` the object we're storing, and we `DECREF` the object we're overwriting, if any.

This is safe, because there's no way for the heap to point at the stack.

We'll also have immutable and interned values. These are marked in the value so you don't have to dereference them, and the refcount itself will be `-1`. The `INCREF` and `DECREF` operations will be performed using atomic operations.

Whenever we `DECREF` an object and it reaches zero, we do not delete it immediately, simply put it on a zero-count table to delete later. One array per thread to avoid threading issues. When we run out of slab memory or the zero count table overflows, it's time to free unused memory. At that point, we bring the threads to a safe point, where the Python stacks are no longer being changed, and we look at the zero count tables, scan for references on the stack and remove them from the zero count tables, and delete and `DECREF` or delete members of these objects. Note that children themselves can be pointed-to from the stacks.

Maybe we build a bloom filter from the stacks to achieve this quickly. No, this doesn't work, we cannot forget to decref some objects due to lossiness, because these objects will never show up again on the ZCT and therefore leak. They can also hold on to large graphs of stuff transitively.

This scheme would naturally place all newly allocated objects on the zero count tables, which is wasteful. Maybe we can have a scheme where bump-allocated objects from slabs don't go on the list, but we have a way to traverse objects on bump-allocated slabs and note what slabs we've allocated from.

Finally, we have to stop mutation of the Python stacks and zero count tables when we do the scan. We can do this with JVM-style safepoints that are tested on loop iterations and either function calls or returns. Long-running C calls pose a bit of a problem. However, the only guarantee we need is that we stop mutating the Python stacks and zero count tables, that is, stop `DECREF`. There is already a system for this in CPython:

```c
Py_BEGIN_ALLOW_THREADS
... Do some blocking I/O operation ...
Py_END_ALLOW_THREADS
```

The original purpose is to release the GIL so that other Python threads can do their thing, but we could repurpose it to declare ourselves at a safe point, for example by atomically decrementing a num-threads-running variable or similar. Care must be taken so that if we're doing a global operation such as GC and a C thread calls `Py_END_ALLOW_THREADS`, then it isn't able to proceed until the global operation is complete.

clovervm will only implement the limited API.

It's important to remember the accumulator as well when we do an allocation, store, safepoint or function call, as these can lead to decref or to-delete overflow that triggers garbage collection. Reserve a spot on the stack frame for the accumulator, and eagerly save it in these cases. Same goes for when we exit jitted code and place registers back on the frame.

Actually, cleverly arrange the opcodes so the accumulator either gets overwritten or consumed by these ops. Then there is no issue.

Accumulators are only live across expressions, not across statements. This helps.

Examples:

- store accumulator into named property
- store accumulator into array element
- call function object pointed to by accumulator, which stores the function object on the stack frame

We have function object, code object, instruction pointer.
