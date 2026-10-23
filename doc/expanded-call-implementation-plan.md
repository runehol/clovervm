# Expanded Call Implementation Plan

Implementation checklist for the accepted
[function-call adaptation design](function-call-adaptation.md), starting from
`3a84a6a2`. Stages 1 and 2 are complete; later stages have not started.

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
shape-cache coverage. Release `check_opcode_frames` passed. Stage 1b was
subsequently committed as `49a50623`.

### 1c: List extension

- [x] Add `ListExtend builder_reg`, taking the source from the accumulator
  and returning `None` in the accumulator on every successful path.
- [x] Implement exact tuple/list native paths, including self-extension from
  the original extent and correct storage growth/reference ownership.
- [x] Add the managed helper for other sources, including list/tuple subclasses,
  using an ordinary loop and the trusted append intrinsic.
- [x] Preserve builder and source across callbacks; propagate failure with
  already appended elements retained and without resuming after the operation.
- [x] Test all three paths, self-extension, subclass overrides, nested calls,
  and partial extension followed by an exception.
- [x] Run required checks and leave changes uncommitted for review.

Verification: eight focused tests passed. Full debug checks passed with 1,672
tests passed and one skipped. Release `check_opcode_frames` passed. Stage 1c was
subsequently committed as `5bf61fab`.

## Stage 2: Keyword construction

Primary surfaces: dict runtime helpers, trusted Python helpers, bytecode,
codegen intrinsic lowering, interpreter handlers, and VM helper ownership.

### 2a.1: Native copy and string-keyed storage primitives

- [x] Verify and reuse `Dict::copy()` for fresh exact-dict copies of both
  storage shapes, preserving order, original key objects, values, and stored
  hashes without invoking Python callbacks.
- [x] Add unchecked string-keyed insertion that reports an existing equal key
  without replacing its value. Require exact string-keyed storage and an exact
  string key; leave callable-specific exception construction to the caller.
- [x] Add string-keyed merge using that uniqueness rule. Report the first
  conflicting key and retain the successfully inserted prefix. Require exact
  string-keyed source and destination storage; invoke no Python callbacks.
- [x] Test fresh copies of both shapes, key/value identity, insertion order,
  duplicate preservation, self-merge, and storage growth.
- [x] Keep opcode wiring in 2a.2 so general-storage behavior is settled before
  installing the handlers. Expanded-call syntax remains disabled.
- [x] Run required checks and leave changes uncommitted for review.

Verification: five focused storage tests passed. Full debug checks passed with
1,677 tests passed and one skipped. The changed dict source also compiled in
release mode. Stage 2a.1 was subsequently committed as `ef48a5f6`.

### 2a.2: Keyword opcodes and managed general-storage operations

- [x] Add encoding, decoding, disassembly, register effects, trusted lowering,
  and interpreter handlers for `BuildCallKeywords callable_reg, dict_reg`,
  `MergeCallKeywords callable_reg, dict_reg`, and
  `AddCallKeyword callable_reg, dict_reg, name_const, value_reg`.
- [x] Use 2a.1 primitives for exact-dict copy and string-keyed merge/insertion.
- [x] Implement managed duplicate-aware general-storage insertion with its own
  generated Function, sharing only the restartable probe-loop emitter with
  existing dict methods. Preserve normal hash/equality
  callbacks and restart probes when callbacks invalidate their state; do not
  recursively enter the interpreter from a native opcode helper.
- [x] Implement exact-dict merge for general storage with rooted source
  key/value/stored-hash entries and indexed traversal across callbacks. Preserve
  original keys and order; promote destination storage as needed. Record the
  initial source entry count, propagate callback/duplicate errors first, then
  check the count after each successful insertion and raise
  `RuntimeError("dict mutated during update")` on a mismatch. Accept value
  replacement and same-size mutations; do not additionally reject source
  generation changes. Keep destination probe-generation restart checks.
- [x] Add shared callable-context duplicate-error construction. Keep arbitrary
  keys until call-entry validation, without binding them to parameters here.
- [x] Keep destinations rooted in their explicit registers across native and
  managed paths; no trailing `Star` is required. All successful paths return
  `None` in the accumulator. Generic mapping inputs, including dict subclasses,
  temporarily raise `NotImplementedError`; their protocol support belongs to 2b.
- [x] Test opcode effects and lowering, direct destination installation,
  subclass-key hash/equality callbacks, callback mutation, non-string transport
  keys, and partial results on failure. Cover source size changes, same-size
  mutations, later value replacement, and callback/duplicate error precedence
  over source-size errors.
- [x] Run required checks and leave changes uncommitted for review.

Verification: 129 focused keyword construction, bytecode-effect, codegen, and
dict regression tests passed. Full debug checks passed with 1,688 tests passed
and one skipped. Release `check_opcode_frames` passed. The known-hash insertion
helper has its own generator, sharing only probe-loop emission with the existing
dict method generator. Stage 2a.2 was subsequently committed as `7c05c73d`.

### 2b: Generic mapping helpers

- [x] Add managed fallback receiving `(callable, destination, source)`.
- [x] Call `source.keys()`, normalize with `ToExactList`, and finish
  materialization before fetching any values from this source.
- [x] Traverse with an ordinary list iterator. Check each duplicate before
  fetching `source[key]`, then insert the value without string-key validation.
- [x] Reuse exact key lists and preserve live mutation behavior: replacement,
  growth, shrinkage, skipped shifted entries, and repeated-key errors.
- [x] Propagate missing mapping protocol, keys/materialization failures, lookup
  failures, and hash/equality failures with the agreed callable context.
  Translate `AttributeError` around the direct `source.keys()` call, including
  its body, to a contextual mapping `TypeError`. Materialization and all later
  callbacks stay outside that boundary; preserve their exceptions, including
  `AttributeError` and `KeyError`. No method-lookup opcode is needed.
- [x] Test exact-list reuse, subclass/iterator materialization, pairs-only
  rejection, duplicate-before-value-lookup, and nested callback rooting.
- [x] Run required checks and leave changes uncommitted for review.

Verification: full debug checks passed with 1,696 tests passed and one skipped,
including all 14 exact-dict/generic keyword construction tests. Release
`check_opcode_frames` passed. Generic mapping support uses an ordinary Python
helper and existing bytecodes, with `AttributeError` translation limited to the
direct `source.keys()` call. Stage 2b changes remain uncommitted for user review.

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
