# clovervm TODO

The earlier "lowest-hanging fruit" checklist is mostly complete. This file now
tracks the most reasonable next steps after reviewing the current codebase,
[README.md](/Users/runehol/projects/clovervm/README.md), the completed
`for`-loop plan, and the longer-term design notes.

## Immediate cleanup and correctness

- [ ] Tighten string literal handling, or explicitly document the narrow subset.
  Why: `src/tokenizer.cpp` still only recognizes a very small double-quoted
  string form with no escapes or single quotes. That limitation is easy to trip
  over, and it is currently larger than the rest of the documented language
  subset implies.
  Where: `src/tokenizer.cpp`, `tests/test_tokenizer.cpp`, `README.md`.

- [ ] Turn `short_vector` assertion failures into runtime exceptions and add tests.
  Why: `src/short_vector.h` still has explicit `TODO` comments for this. Bounds
  and type failures should not rely on debug-only assertions once more runtime
  features start using the container.
  Where: `src/short_vector.h`, targeted tests.

- [ ] Replace generic runtime errors on important slow paths with specific exceptions.
  Why: arithmetic overflow and non-SMI fallbacks in `src/interpreter.cpp` still
  collapse to `"Clovervm exception"`. Improving those failures will make the VM
  much easier to debug and will keep future behavior changes testable.
  Where: `src/interpreter.cpp`, `tests/test_interpreter.cpp`.

## Next language and runtime slice

- [ ] Make local-slot discovery less ad hoc before adding more statement forms.
  Why: the current TODO list and the present codegen structure both point to
  local analysis as a weak spot. Scanning function bodies for locals up front
  will make control flow, nested blocks, and upcoming syntax work less brittle.
  Where: `src/codegen.cpp`, `src/scope.cpp`, function-related tests.

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

## Data structures and object model

- [ ] Stage the hidden-class object model described in `doc/object-model-plan.md`.
  Why: the long-term direction is shape-based objects with slot layouts that
  can be specialized by the JIT. The immediate next work is the first
  implementation slice in that doc: shapes, instances, generic attribute
  bytecodes/helpers, and VM-native exception propagation.
  Where: new runtime object types, attribute bytecodes, and interpreter/JIT
  support.

- [ ] Turn the placeholder scope/container storage into real VM-managed arrays.
  Why: both `src/indirect_dict.h` and `src/scope.h` still say some backing
  structures "need to be CL arrays at some point". This is an important step if
  the VM is going to grow beyond the current narrow subset without leaning on
  host-side containers forever.
  Where: `src/indirect_dict.h`, `src/scope.h`, related allocation/runtime code.

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

## Performance and benchmarking

- [ ] Use the new benchmark harness to establish a small tracked baseline before further feature work.
  Why: the benchmark target now covers recursive calls plus `while` and `for`
  loops. Capturing a baseline and adding one or two more workloads tied to the
  next feature slice will make future regressions visible.
  Good candidates: function-call-heavy code, builtin calls, and container-heavy
  loops once lists/dicts exist.
  Where: `benchmark/`, benchmark documentation, and release-build workflow.
