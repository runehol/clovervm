# clovervm TODO

The earlier "lowest-hanging fruit" checklist is mostly complete. This file now
tracks the most reasonable next steps after reviewing the current codebase,
[README.md](/Users/runehol/projects/clovervm/README.md), the completed
`for`-loop plan, and the longer-term design notes.

## Top priority (next implementation slice)

- [ ] Implement constructor semantics for class instantiation (`__init__` call path).
  Why: this is currently one of the biggest semantic gaps in day-to-day Python
  behavior and blocks realistic class-oriented programs. `Cls(...)` needs to
  drive instance creation plus `__init__` invocation with correct argument
  handling and error behavior.
  Scope:
  1. call `__init__` automatically during `Cls(...)`
  2. support argument forwarding and arity errors
  3. define/validate return-value behavior for `__init__`
  4. add interpreter-first tests for constructor success and failure modes
  Where: class call/instantiation runtime path, method lookup/call helpers,
  `tests/test_interpreter.cpp`.

- [x] Build the namespace/mapping substrate + VM-managed arrays (foundation layer)
      Why: This was the prerequisite for making namespace-like storage real instead of aspirational. `RawArray<T>` / `ValueArray<T>` now back `IndirectDict` and `Scope`, and `Scope` now uses the slot-centered parallel-array shape described in the design docs rather than `std::vector`.

- [ ] Implement list/dict types and literals + subscripting end-to-end (next language unlock)
      Why: Gives immediate practical expressiveness (most Python-ish programs need containers). Creates pressure to harden object model/mappings in realistic ways (good pressure). Enables richer benchmark workloads that matter.

- [ ] Replace generic runtime failures with specific VM exceptions (debuggability + compatibility)
      Why: Improves test specificity and speeds future refactors. Essential prerequisite for proper iterator protocol and broader Python semantics.

- [ ] Local-slot discovery / function-scope analysis rewrite (compiler robustness)
      Why: Reduces brittleness as grammar expands (containers, richer call signatures, more statements). Avoids compounding technical debt in codegen and scope interactions.

- [ ] Build a simple version of the reclaimer that discovers Values in objects and decrefs them
      Why: This is best done early for correctness, before the number of object kinds get too unwieldy.

## Immediate cleanup and correctness

- [ ] Replace generic runtime errors on important slow paths with specific exceptions.
  Why: arithmetic overflow and non-SMI fallbacks in `src/interpreter.cpp` still
  collapse to `"Clovervm exception"`. Improving those failures will make the VM
  much easier to debug and will keep future behavior changes testable.
  Where: `src/interpreter.cpp`, `tests/test_interpreter.cpp`.

- [ ] Clean up built-in function calls. Right now we detect the function kind and dispatch
      to builtins using a different calling convention. But this is error-prone and multiplies
      with the number of call opcodes and special cases. Instead, I'd like us to generate a small
      python function trampoline that indirects into the builtin function using a purpose-built
      opcode. This way, we can also support multiple builtin-function calling conventions,
      which might be very handy as not everyone needs full arg parsing, and we might want a
      CloverVM convention that's slimmer than the full Python C API one

## Next language and runtime slice


- [ ] Add one more builtin on top of the new builtin-function path.
  Why: `range` proved out the mechanism. Adding a small second builtin such as
  `print` would validate that builtin lookup, arity checks, and call dispatch
  are becoming a reusable runtime interface rather than a one-off for loops.
  Where: `src/virtual_machine.cpp`, `src/interpreter.cpp`, interpreter tests.

- [ ] Extend the supported Python subset in the order that unlocks real programs.
  Why: the implementation is now strong enough on expressions, functions, and
  loops that the next bottleneck is missing data access and containers, not loop
  control itself.
  Suggested order:
  1. list and dict literals
  2. subscripting
  3. attribute access / method-call syntax
  4. richer call syntax
  5. remaining statements such as `import`, `class`, and `with`
  Where: parser, codegen, interpreter, and focused interpreter-first tests.

- [ ] For loops and iterator protocol. We need to implement the iterator
  protocol properly once we've landed an object model and exceptions. Also note
  that currently range() returns a RangeIterator. That's not actually right - it
  should return a Range, and calling iter() on that should return a RangeIterator.


## Data structures and object model

- [ ] Start implementing the dictionary design described in `doc/dictionaries.md`.
  Why: dictionaries unlock globals/scopes, object storage, and eventually Python
  dict semantics. The design note is specific enough that this can become a real
  implementation plan rather than staying aspirational.
  Where: new dictionary runtime types plus scope integration.

## Memory-management architecture

- [ ] Write down a staged implementation plan for `doc/refcounting-and-safepoints.md`.
  Why: the refcounting note has good long-term ideas, but it is still a sketch.
  The project would benefit from a concrete sequence such as: deferred
  refcounting invariants, zero-count tables, safepoint polling, then
  multi-threaded coordination.
  Where: new design doc or an expanded version of
  `doc/refcounting-and-safepoints.md`.

- [ ] Decide what near-term GC / lifetime invariants must hold before more heap objects land.
  Why: upcoming work on dictionaries, collections, and richer objects will
  multiply the number of refcounted heap paths. It is worth locking down the
  invariants early instead of retrofitting them after object graphs get larger.
  Where: memory-management docs, `src/refcount.h`, allocator and object tests.
