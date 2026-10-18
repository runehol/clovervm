# Expanded Call Implementation Plan

Implementation checklist for the accepted
[function-call adaptation design](function-call-adaptation.md), starting from
`3a84a6a2`. No expanded-call implementation stages are complete yet.

## Working rules

- **Do not auto-commit or push. Leave changes uncommitted for the user to
  review. Commit or push only when explicitly requested.**
- Cross off each item as it is completed, including verification. Do not mark a
  stage complete while its required checks remain outstanding.
- Implement one checkpoint at a time. Complete its verification and leave a
  reviewable diff before moving to the next checkpoint.
- Use the accepted design as authority. Resolve concrete frame, operand, and
  helper details in the checkpoint's readiness review. Discuss material changes
  to semantics, ownership, cache behavior, or interpreter invariants before
  implementing them.
- Keep expanded-call syntax unsupported until its execution path is complete.
  Earlier checkpoints can use trusted intrinsic and bytecode-level tests.
- Format touched C++ files. Run `ninja -C build-debug all check` for each
  completed implementation checkpoint. For frame-sensitive interpreter work,
  also run `cmake --build build-release --target check_opcode_frames`.
- Before any explicitly requested push, run
  `ninja -C build-release all check` at the exact HEAD being pushed.

## Stage 1: List primitives and conversions

Primary surfaces: `src/builtin_types/list.*`, `src/bytecode/bytecode.def`, bytecode
builders/decoders/disassembly, `src/compiler/codegen.*`,
`src/bootstrap/builtin_implementations.py`, and interpreter/helper ownership in
`src/runtime/`.

### 1a: Native builder primitives

- [x] Inspect existing list append/storage growth and list-to-tuple helpers;
  reuse unchecked storage operations where their invariants fit.
- [x] Add `ListAppend builder_reg`, taking the item from the accumulator.
- [x] Add `ListToTuple builder_reg`, reading the exact list from that register
  and replacing it with the resulting exact tuple. It has no accumulator input
  or result and does not mutate the source list object.
- [x] Add trusted compiler intrinsics for helper Python code. Keep them
  compiler-recognized, without public builtin lookup.
- [x] Wire encoding, decoding, disassembly, register effects, and safe JIT
  fallback according to existing patterns. Update the hot-handler manifest as
  required by runtime instructions.
- [x] Test empty/nonempty builders, ordering, retained element references, and
  builder contents after conversion. Pin down intrinsic lowering with focused
  codegen tests.
- [x] Run required checks and leave changes uncommitted for review.

Verification: six focused tests passed; `ninja -C build-debug all check` passed
with 1,657 tests passed and one skipped. Release `check_opcode_frames` passed.
The opcode handlers contain their bodies directly and are not on the hot-handler
list. Stage 1a was subsequently committed as `90b35a36`.

### 1b: Exact-container conversion and managed helper entry

- [x] Use `code_object->get_first_free_arg_encoded_reg()` for helper arguments
  without an extra opcode operand. Confirm alignment, saved return
  PC, stack capacity, ordinary accumulator return, and exception path before
  coding. Use ordinary `Star` for result storage, without conversion-specific
  finish instructions or accumulator preservation scratch.
- [x] Add `ToExactTuple value_reg`, reading the source register without changing
  it and always writing the accumulator on success: reuse exact tuples;
  otherwise enter the VM-owned managed collection helper and convert its list builder to a tuple.
- [x] Add `ToExactList value_reg`, reading the source register without changing
  it and always writing the accumulator on success: reuse exact lists;
  otherwise enter the VM-owned managed collection helper returning a fresh exact list.
- [x] Cache exact tuple/list root shapes on `ThreadState`, following the string
  cache, and verify bootstrap registration and newly created threads.
- [x] Add trusted conversion intrinsics for use by later mapping helpers.
- [x] Retain helper Functions on the VM following existing builtin/helper
  ownership patterns. Preserve source, builder, and iterator roots across calls.
- [x] Use normal managed frame entry, returns, iteration ICs, and exception
  unwinding; do not recursively enter the interpreter from C++.
- [x] Test exact-container identity, list/tuple subclass iterator overrides,
  generic iterables and existing iterators, empty sources, noniterables, and
  failures during iterator acquisition and advancement.
- [x] Verify nested callbacks preserve caller live registers, conversion leaves
  its source register unchanged, both paths overwrite the accumulator, and
  execution resumes only after successful conversion.
- [x] Run required checks and leave changes uncommitted for review.

Verification: seven focused conversion/codegen tests passed. Full debug checks
passed with 1,665 tests passed and one skipped, including bootstrap/new-thread
shape-cache coverage. Release `check_opcode_frames` passed. Stage 1b changes
remain uncommitted for user review.

### 1c: List extension

- [ ] Add `ListExtend builder_reg`, taking the source from the accumulator.
- [ ] Implement exact tuple/list native paths, including self-extension from
  the original extent and correct storage growth/reference ownership.
- [ ] Add the managed helper for other sources, including list/tuple subclasses,
  using an ordinary loop and the trusted append intrinsic.
- [ ] Preserve builder and source across callbacks; propagate failure with
  already appended elements retained and without resuming after the operation.
- [ ] Test all three paths, self-extension, subclass overrides, nested calls,
  and partial extension followed by an exception.
- [ ] Run required checks and leave changes uncommitted for review.

## Stage 2: Keyword construction

Primary surfaces: dict runtime helpers, trusted Python helpers, bytecode,
codegen intrinsic lowering, interpreter handlers, and VM helper ownership.

### 2a: Exact-dict construction and explicit insertion

- [ ] Add the agreed operands and register effects:
  `BuildCallKeywords callable_reg, dict_reg`,
  `MergeCallKeywords callable_reg, dict_reg`, and
  `AddCallKeyword callable_reg, dict_reg, name_const, value_reg`.
- [ ] Implement fresh exact-dict copy for Build and duplicate-rejecting Merge
  for exact-dict sources. Preserve insertion order and original key objects.
- [ ] Support both dict storage shapes and promotion through existing dict
  semantic APIs; keep arbitrary keys until call-entry validation.
- [ ] Implement Add using an exact string constant and a value register,
  rejecting duplicates rather than overwriting values.
- [ ] Keep destinations rooted in their explicit registers; no trailing `Star`
  is required to install a Build/Merge result.
- [ ] Test freshness, first-copy versus merge duplicates, existing subclass-key
  hash/equality behavior, non-string transport keys, and failures during merge.
- [ ] Run required checks and leave changes uncommitted for review.

### 2b: Generic mapping helpers

- [ ] Add managed fallback receiving `(callable, destination, source)`.
- [ ] Call `source.keys()`, normalize with `ToExactList`, and finish
  materialization before fetching any values from this source.
- [ ] Traverse with an ordinary list iterator. Check each duplicate before
  fetching `source[key]`, then insert the value without string-key validation.
- [ ] Reuse exact key lists and preserve live mutation behavior: replacement,
  growth, shrinkage, skipped shifted entries, and repeated-key errors.
- [ ] Propagate missing mapping protocol, keys/materialization failures, lookup
  failures, and hash/equality failures with the agreed callable context.
- [ ] Test exact-list reuse, subclass/generator materialization, pairs-only
  rejection, duplicate-before-value-lookup, and nested callback rooting.
- [ ] Run required checks and leave changes uncommitted for review.

## Stage 3: Expanded call entry and end-to-end support

### 3a: Binding-plan representation and uncached binder

- [ ] Readiness review: agree the concrete temporary-plan representation,
  managed binder signature/result, owning layer, rooted references, and handoff
  to native plan application. This is the remaining substantial internal sketch.
- [ ] Resolve functions and eligible constructor thunks through existing call
  machinery; keep callable/signature caching independent of binding-plan caching.
- [ ] Validate all keyword keys before matching, accepting string subclasses.
- [ ] Match incoming keywords in insertion order against keyword-bindable
  parameters in declaration order. Use native exact-string comparisons and
  managed rich equality/truth conversion for subclass keys, without key hashing.
- [ ] Track positional and keyword fills; report keyword errors before excess
  positional arguments, then resolve defaults and missing required arguments.
- [ ] Build a temporary plan without changing the live IC. Failed plans are
  neither applied nor installed. Defer keyword binding-plan caching to stage 4.
- [ ] Record `ReuseTransport` when no keyword matched named parameters, or
  `BuildLeftovers` when some matched and the callee has a kwargs collector.
- [ ] Test binding order, comparison failures, positional-only names, defaults,
  repeated formal fills, and error precedence against the accepted design.
- [ ] Run required checks and leave changes uncommitted for review.

### 3b: Plan application and CallExpanded

- [ ] Add `CallExpanded callable_reg, containers_reg, first_arg_reg, cache_idx`.
  Require adjacent exact tuple/fresh exact dict inputs, both always present.
- [ ] Keep input containers and callable outside the outgoing parameter window.
  Fill it only after successful validation, using existing prepared frame entry.
- [ ] Apply positional values, defaults, tuple-suffix varargs, and collector
  actions. Preserve transport; build leftovers only after successful planning.
- [ ] Implement pure collection with key validation and direct container handoff.
- [ ] Preserve the agreed collector incompatibility: do not reproduce CPython's
  extra collector hash/equality calls or their interleaving with matching.
- [ ] Handle collector construction failures without entering the callee.
- [ ] Test frame alignment, nested calls, roots, unwind/return behavior, both
  collector actions, and constructor-thunk forwarding.
- [ ] Wire safe interpreter fallback/JIT effects and run required checks.
- [ ] Leave changes uncommitted for review.

### 3c: Parser and call precode

- [ ] Consult CPython's `Parser/Python.asdl`; extend existing call-argument AST
  nodes for starred and double-starred arguments without mixing in binding.
- [ ] Lower mixed positional arguments through list construction/extension.
- [ ] For exactly one starred source and no ordinary positional arguments,
  evaluate/root the source before keywords but emit `ToExactTuple` afterward.
- [ ] Evaluate each consecutive explicit keyword run into temporary registers
  before emitting its Add operations; interleave runs and mapping merges in
  keyword order.
- [ ] Reserve adjacent transport registers and a separate outgoing-window
  anchor. Always build a fresh keyword dict, including for tuple-only expansion.
- [ ] Preserve ordinary call opcodes for calls without expansion and explicit
  rejection of callable forms outside the existing supported scope.
- [ ] Enable expanded-call syntax only once its complete path works.
- [ ] Add end-to-end tests covering mixed/repeated sources, late conversion,
  early duplicate errors, deferred non-string errors, collectors, subclass keys,
  defaults, and constructor calls. Add structural codegen tests for the agreed
  operand layout and evaluation staging.
- [ ] Run required checks and leave changes uncommitted for review.

## Stage 4: Optional exact-string binding-plan cache

- [ ] Measure uncached expanded calls before deciding to implement this stage.
- [ ] If justified, install only successfully validated exact-string plans and
  use the same application path for temporary and cached plans.
- [ ] Guard callable validity, counts, exact-string keys, and ordered key
  contents. Read current defaults rather than caching default values.
- [ ] Bypass on subclass keys without evicting a valid exact-string plan;
  non-string keys reach ordinary validation and fail there.
- [ ] Test misses/hits, changing names/order/counts, callable invalidation,
  default changes, cache ownership, and subclass comparison side effects.
- [ ] Measure before/after; run required checks and leave changes uncommitted
  for review. Record deferral if measurements do not justify the cache.
