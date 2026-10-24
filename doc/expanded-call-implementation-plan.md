# Expanded Call Implementation Plan

Implementation checklist for the accepted
[function-call adaptation design](function-call-adaptation.md), starting from
`3a84a6a2`. Stages 1 through 3 (including 3d) are complete; stage 4 has not started.

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

Implementation: `src/runtime/expanded_call.{h,cpp}`; focused tests in
`tests/test_expanded_call.cpp`. `build_expanded_call_binding_plan` takes an
already-resolved, rooted Function, positional count, and rooted exact transport
dict and returns `Expected<ExpandedCallBindingPlan>`. Callable resolution stays
in the existing call machinery; the binder does not duplicate it.

- [x] Agree the native, IC-compatible plan representation and C++ binder
  direction, recorded in the accepted design. No Python binder or managed
  tuple plan is needed.
- [x] Implement `ExpandedCallBindingPlan` in the runtime call-adaptation layer:
  `keyword_destinations` and `default_slots` are vectors of logical parameter
  indices; `UnmatchedKeyword = UINT32_MAX` marks collector entries;
  `kwargs_action` is `None`, `ReuseTransport`, or `BuildLeftovers`.
  The plan owns no Python references. Keep the Function and input containers
  rooted outside it, and return the plan through a fallible native builder.
- [x] Validate all keyword keys before matching, accepting string subclasses.
- [x] Match incoming keywords in insertion order against keyword-bindable
  parameters in declaration order. Use native exact-string comparisons and
  the existing C++-to-managed `ThreadState::test_equal` bridge for subclass-key
  rich equality/truth conversion, without key hashing. Accept interpreter
  re-entry for this uncommon case; no binder-specific comparison IC is required.
- [x] Track positional and keyword fills; report keyword errors before excess
  positional arguments, then resolve defaults and missing required arguments.
- [x] Build a temporary plan without changing the live IC. Failed plans are
  neither applied nor installed. Defer keyword binding-plan caching to stage 4.
  Application must accept the same const native plan whether it is temporary
  or later held in an IC. Formal names stay in `FunctionKeywordRemap`; incoming
  keys stay in transport. Cache guards, positional count, and owned ordered
  exact-string names belong outside the plan in a future cache wrapper.
- [x] Record `ReuseTransport` when no keyword matched named parameters, or
  `BuildLeftovers` when some matched and the callee has a kwargs collector.
- [x] Test binding order, comparison failures, positional-only names, defaults,
  repeated formal fills, and error precedence against the accepted design.
- [x] Run required checks and leave changes uncommitted for review.

Verification: all 17 native binder tests passed in the full debug suite.
`ninja -C build-debug all check` passed with 1,713 tests passed and one
skipped. No interpreter opcodes or frame-entry code changed. Changes remain
uncommitted for review.

### 3b: Plan application and CallExpanded

Implementation: native application in `src/runtime/expanded_call.{h,cpp}`,
`CallExpanded` dispatch and shared callable resolution in `interpreter.cpp`,
and a dedicated `ExpandedCallInlineCache` for the target and adapter mode.
The cache contains no keyword names or binding plan. Bytecode effects and
interpreter fallback are covered by execution and JIT translation tests.

- [x] Resolve functions and eligible constructor thunks through existing call
  machinery before invoking the binder; do not duplicate resolution in the
  binder. Keep callable/signature caching independent of binding-plan caching,
  and defer positional arity errors until after keyword matching.
- [x] Add `CallExpanded callable_reg, containers_reg, first_arg_reg, cache_idx`.
  Require adjacent exact tuple/fresh exact dict inputs, both always present.
- [x] Keep input containers and callable outside the outgoing parameter window.
  Fill it only after successful validation, using existing prepared frame entry.
- [x] Apply positional values, defaults, tuple-suffix varargs, and collector
  actions. Preserve transport; build leftovers only after successful planning.
- [x] Implement pure collection with key validation and direct container handoff.
- [x] Preserve the agreed collector incompatibility: do not reproduce CPython's
  extra collector hash/equality calls or their interleaving with matching.
- [x] Handle collector construction failures without entering the callee.
- [x] Test frame alignment, nested calls, roots, unwind/return behavior, both
  collector actions, and constructor-thunk forwarding.
- [x] Wire safe interpreter fallback/JIT effects and run required checks.
- [x] Leave changes uncommitted for review.

Verification: 15 expanded-call execution tests, 17 binder tests, and the JIT
fallback test passed. `ninja -C build-debug all check` passed with 1,729 tests
passed and one skipped. Release `check_opcode_frames` passed. Expanded syntax
remains disabled until 3c; changes are uncommitted for review.

### 3c: Parser and precode for straight expanded calls

Scope: ordinary Function calls and eligible constructor calls. Expanded
attribute/method-call forms are a separate milestone in 3d. Implementation is
in the parser, AST rendering, scope analysis, and call codegen; syntax and
observable timing tests accompany the existing binder/opcode tests.

Agreed implementation sketch:

- Keep the existing argument sequence. Add `CALL_ARGUMENT_STARRED(value)`;
  represent `**mapping` as `CALL_ARGUMENT_KEYWORD(value, name=None)`, following
  CPython's starred expression and optional keyword-name model.
- Accept repeated expansions, but reject ordinary positional arguments after
  keywords, iterable unpacking after mapping unpacking, and repeated explicit
  keyword names.
- Add `codegen_expanded_function_call`, selected only for calls containing an
  expansion. Reject expanded attribute-call targets in codegen until 3d.
- Evaluate the callable, then positional/starred expressions, then keywords,
  even when a starred argument appears textually after a keyword. Scope
  analysis must follow the same evaluation order. Consecutive explicit keyword
  runs are determined within the keyword sequence, excluding starred arguments.
- Reuse existing container opcodes and the lone-star late-conversion rule;
  emit `CallExpanded` with adjacent containers and a separate outgoing anchor.
  Ordinary call lowering stays unchanged. Other argument-parser users, such
  as class bases and trusted intrinsics, retain explicit restrictions.

- [x] Consult CPython's `Parser/Python.asdl`; extend existing call-argument AST
  nodes for starred and double-starred arguments without mixing in binding.
- [x] Lower mixed positional arguments through list construction/extension.
- [x] For exactly one starred source and no ordinary positional arguments,
  evaluate/root the source before keywords but emit `ToExactTuple` afterward.
- [x] Evaluate each consecutive explicit keyword run into temporary registers
  before emitting its Add operations; interleave runs and mapping merges in
  keyword order.
- [x] Reserve adjacent transport registers and a separate outgoing-window
  anchor. Always build a fresh keyword dict, including for tuple-only expansion.
- [x] Preserve ordinary call opcodes for calls without expansion and explicit
  rejection of expanded attribute/method-call forms until 3d. Keep other
  unsupported callable forms explicit.
- [x] Enable expanded-call syntax only once its complete path works.
- [x] Add end-to-end tests covering mixed/repeated sources, late conversion,
  early duplicate errors, deferred non-string errors, collectors, subclass keys,
  defaults, and constructor calls. Add structural codegen tests for the agreed
  operand layout and evaluation staging.
- [x] Run required checks and leave changes uncommitted for review.

Verification: all 20 new parser, codegen, syntax, and scope-analysis tests
passed. `ninja -C build-debug all check` passed with 1,749 tests passed and one
skipped. Attribute-call rejection is in codegen; the parser accepts these forms
for 3d. Scope-order regression coverage now uses end-to-end Python tests with walrus
expressions inside call arguments, replacing the manually constructed AST test.
No interpreter handlers changed. Changes remain uncommitted for review.

Walrus test follow-up: both Python regression scripts passed in Clover and
CPython. Full debug checks passed with 1,763 tests passed and one skipped.

### 3d: Expanded method calls

Start after straight expanded calls work end to end in 3c. Preserve the existing
distinction between function-call and method-call opcodes.

Agreed sketch: add one fused `CallMethodAttrExpanded` opcode, reusing the
existing method lookup/binding helpers and expanded callable cache. Keep the
accepted late-lookup behavior, matching ordinary method calls; fixing lookup
timing is separate work across all argument forms. Do not add `LoadMethod`.
Represent positional input as a borrowed tuple plus an optional implicit-self
prefix (`not_present` means no prefix). The binder still takes only a Function
and positional count; application reads the prefixed view without allocating a
tuple just to prepend self. Reuse expansion precode for both call forms.

Encoding: `CallMethodAttrExpanded receiver_reg, name_const, lookup_cache,
call_cache, containers_reg, first_arg_reg` (seven bytes). The containers remain
an adjacent exact tuple/fresh exact dict pair, outside the outgoing window.
`ExpandedCallPositionalArguments` supplies indexed access to the tuple with its
optional prefix. Application reuses the tuple when it is exactly the varargs
suffix; a collector that includes self gets a newly packed tuple. The call
cache still stores only callable resolution and adapter mode, not keyword plans.

- [x] Readiness review: settle `CallMethodAttrExpanded` operands, method-lookup
  timing, receiver binding, and lookup/call cache contracts using the existing
  positional and keyword method-call paths.
- [x] Settle how implicit `self` participates in positional binding and plan
  application without requiring a tuple copy just to prepend the receiver.
- [x] Reuse the expansion precode, native binding plan, and application path;
  keep method lookup and receiver binding outside the binder.
- [x] Enable expanded attribute/method-call lowering through its separate
  opcode while preserving ordinary method-call lowering.
- [x] Test bound receivers, targets without an implicit receiver, defaults,
  collectors, lookup invalidation, evaluation order, nested calls, and exception
  unwinding. Pin down opcode operands and register effects with structural tests.
- [x] Run full debug checks and release opcode-frame checks; leave changes
  uncommitted for review.

Verification: all 60 focused expanded-call, codegen, and JIT-fallback checks
passed. `ninja -C build-debug all check` passed with 1,775 tests passed and one
skipped. Release `check_opcode_frames` passed. Changes remain uncommitted for
review; the shared late method-lookup mismatch is intentionally unchanged.

## Stage 4: Exact-string binding-plan cache

Measurements justify proceeding with one cached binding plan per expanded-call
site. The release named-parameter combined-call profile attributed approximately
38% of samples to building and destroying the uncached plan, including its
temporary allocations. The builder's `filled` scratch storage now uses
`absl::InlinedVector<bool, 16>` (`d21e5ad9`); stored plans retain their existing
vector representation.

Agreed representation:

```cpp
struct ExpandedCallCachedPlan {
    uint32_t positional_count;  // Includes implicit self, when present.
    std::vector<Owned<TValue<String>>> keyword_names;
    ExpandedCallBindingPlan plan;
};

struct ExpandedCallInlineCache {
    // Existing callable, resolved Function, validity, and mode fields.
    std::unique_ptr<ExpandedCallCachedPlan> binding_plan;
};
```

The entry is allocated only when installing a cacheable plan. Existing callable
resolution and validity guards remain in the IC and also guard its subordinate
plan; replacing the target clears the plan. Function code and signature are
currently immutable from Python, so this stage needs no new signature-version
mechanism. No opcode or reference-free binding-plan representation changes are
required. Straight and method calls share the existing frame-entry path.

- [x] Measure uncached expanded calls and agree to proceed with caching.
- [ ] Add the optional out-of-line entry, retaining ordered keyword names with
  `Owned<TValue<String>>`, following the native-container ownership pattern of
  `CodeObject::constant_table`. Keep the existing plan vectors out of the IC.
- [ ] Add explicit deep-copy construction/assignment following
  `KeywordCallInlineCache`: `BytecodeDecoder` copies the IC tables. Keep moves
  cheap and release entries on replacement, clearing, and destruction. Use the
  current C++20 facilities; do not introduce a general deep-copy pointer wrapper.
- [ ] Clear the subordinate plan whenever callable resolution selects a new
  target, including after constructor lookup invalidation.
- [ ] Preserve the pure-collector fast path. For general calls, guard total
  positional count, keyword count, exact-string incoming keys, and ordered key
  contents using `string_eq`; a successful guard also proves key validity.
- [ ] On a hit, borrow the stored plan and use the existing application path.
  Read current values and defaults rather than caching them. Guarding and
  applying an exact-string plan must not invoke Python callbacks.
- [ ] On a cacheable miss, build and apply a local plan successfully, then move
  it into a newly allocated cache entry. Never install a failed plan or build
  directly into live cache storage.
- [ ] Bypass on subclass keys without evicting a valid plan for the same target.
  Keep callback-capable plans local; do not borrow a cache entry across Python
  callbacks. Non-string keys reach ordinary validation and fail with unchanged
  error precedence.
- [ ] Test hits and misses, changing values/names/order/counts, equal noninterned
  names, implicit method receivers, target invalidation, and current defaults.
- [ ] Test retained-name lifetime, cache clearing/replacement/copying, and
  subclass comparison side effects including reentrant calls.
- [ ] Run full debug checks and release `check_opcode_frames`; compare the same
  13 expanded-call benchmark cases before/after. Leave changes uncommitted for
  review.
