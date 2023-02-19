# clovervm - Clover Virtual Machine
Experiments with JIT compilation. Very much work-in-progress.


## Planned design points

- Python 3 source language.
- V8-inspired register/accumulator byte code.
- Value representation with integers, bools and None represented without indirection and memory allocation.
- JIT compilation to native code for selected architectures.
- Type feedback for specialising the native code.
- Hidden classes to handle Python objects with C struct-like performance.
- Python C API compatibility for supporting the extension ecosystem.
- Memory management using deferred refcounting - only references on the heap are refcounted, not references on the stack. This means only stores into objects trigger refcounting.
- No GIL, fast multithreading.