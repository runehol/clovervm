# Get-Item Inline Cache Spike

This document stages the first implementation spike for fast operator dispatch.
The spike is intentionally narrower than the full design in
`fast-operator-dispatch.md`: it only explores a monomorphic inline cache for
ordinary `obj[key]` special-method lookup and call replay.

The goal is to learn whether clovervm can cache the selected `__getitem__`
special-method lookup at a bytecode site, validate that lookup with existing
class/MRO validity machinery, and replay the selected method without changing
Python-visible behavior.

## Scope

Implement only:

- `LoadSubscript` / get-item subscription.
- Minimal `__getitem__` methods for the builtin container types selected for
  spike tests, such as `list`, `tuple`, `str`, or `dict`.
- One monomorphic cache entry per cache index.
- Operand-shape guards for the container and key.
- A guarded dunder-method lookup plan for `type(container).__getitem__`.
- Replay of cacheable function-shaped `__getitem__` calls through existing call
  machinery.
- Conservative cache misses for all unsupported lookup or binding cases.
- Tests that exercise hit, miss, invalidation, and side-effect behavior.

Do not implement in this spike:

- trusted native handlers
- trusted-publication state in `ThreadState`
- compound enter/continue opcodes
- binary, in-place, comparison, store-item, or delete-item caches
- descriptor-general replay
- class subscription through `__class_getitem__`
- polymorphic inline caches or megamorphic state
- exception-path caching

The spike is allowed to be slower than the eventual design. It should answer
the semantic and integration questions first.

## Expected Runtime Shape

The cache entry should represent the first cacheable action selected by the
normal get-item dispatcher:

```text
guard:
    shape(container) == cached_container_shape
    shape(key) == cached_key_shape

validity:
    lookup_validity_cell proves that dunder lookup for
    type(container).__getitem__ is still valid

action:
    call the cached selected __getitem__ method
```

On a cache miss, the opcode should run the normal get-item behavior by
resolving an executable `__getitem__` call plan. If that plan is cacheable, the
opcode may install or replace the monomorphic entry after lookup, binding,
callable validation, and call adaptation have succeeded. The cache stores the
ability to replay the selected method call; it must not cache the result value
or any exception raised by the method body.

On a cache hit, the opcode may skip dunder-method lookup. It must still execute
the selected `__getitem__` method and return that method's result.

Returning the `NotImplemented` singleton from `__getitem__` is not a protocol
continuation for subscription. It is just the result of the subscription
operation.

## Why No Continuation System

The spike does not need an operator continuation opcode. Get-item subscription
has no later protocol state to resume after the selected `__getitem__` call:
the method result is the subscription result.

The spike still needs some existing way to call a cached Python function-shaped
dunder method. If the current call path is too awkward or too expensive, that is
useful information from the spike, but it should not force the first spike to
design the eventual continuation system.

Trusted handlers also do not require continuation state. A future trusted
handler hit validates guards, calls a native handler, and returns the handler's
result. Trusted-publication state is only a possible miss-path mechanism for
deciding whether to install such a handler, and is out of scope here.

## Staging

### Stage 1: Locate Existing Boundaries

- [x] Find the current `LoadSubscript` bytecode definition, encoder/decoder, and
  interpreter handler.
- [x] Find the current generic get-item or subscription helper.
- [x] Find the current special-method call path used for function-shaped dunder
  methods.
- [x] Find the existing inline-cache storage pattern on code objects.
- [x] Find the existing validity-cell API for class MRO shape and contents.
- [x] Identify how code objects expose cache payloads to GC tracing.
- [x] Identify which builtin containers already have native subscription helpers
  but do not expose `__getitem__` as a normal special method.

Checkpoint:

- [x] Write down the exact files and helper functions the spike will touch.
- [x] Do not change bytecode format until the cache payload and call path are clear.

Stage 1 findings:

- Bytecode definition and printing:
  - `src/bytecode.h` defines `Bytecode::LoadSubscript`,
    `Bytecode::StoreSubscript`, and `Bytecode::DelSubscript`.
  - `src/code_object_print.h` prints `LoadSubscript` as a two-byte
    opcode/register instruction today.
- Bytecode emission:
  - `src/code_object_builder.cpp::emit_load_subscript` currently emits
    `Bytecode::LoadSubscript` through `emit_opcode_reg`.
  - `src/code_object_builder.h` already has cache allocation patterns for
    attribute, module-global, function-call, and keyword-call ICs.
  - New get-item cache allocation should follow the existing
    `allocate_*_cache()` pattern and keep the cache index as a `uint8_t`
    operand unless the spike finds a real reason to widen it.
- Codegen entry points:
  - `src/codegen.cpp::codegen_subscript_assignment`,
    `codegen_subscript_target_delete`, and expression lowering around
    `emit_load_subscript` are the places that will observe any bytecode operand
    shape change.
- Interpreter entry point:
  - `src/interpreter.cpp::op_load_subscript` is a hot opcode handler with
    `START(2)`. It currently calls `load_subscript(fp[reg], accumulator)`,
    propagates pending exceptions, reports `not_present` as a subscript error,
    and completes synchronously.
  - Store/delete subscription use sibling handlers, but they are out of scope
    for this spike.
- Current subscription helpers:
  - `src/subscript.h::load_subscript_fast` handles exact `list[smi]`,
    `tuple[smi]`, `dict[key]`, and `slotdict[key]` directly before the slow
    path.
  - `src/subscript.cpp::load_subscript_slow` handles list, tuple, string, dict,
    and slotdict native-layout cases. It does not perform dunder-method lookup
    for user objects today; unknown receivers return `Value::not_present()`.
  - This means the first IC spike needs a new get-item protocol path rather
    than merely caching the existing slow helper.
- Existing special-method lookup and call machinery:
  - `src/attr.cpp::resolve_special_method_read_descriptor` performs
    special-method lookup through `type(obj)`/MRO and uses
    `ClassObject::get_or_create_mro_shape_and_contents_validity_cell()` when
    the result is cacheable.
  - `src/attr.cpp::load_method_from_plan` / `load_special_method` replay
    cacheable lookup plans and bind function receivers.
  - `src/interpreter.cpp::op_call_special_method` and
    `op_call_special_method_slow` are the best existing model for
    function-shaped special-method calls with both an attribute-read IC and a
    `FunctionCallInlineCache`.
  - That opcode enters a callee frame and returns after the full call
    instruction length. `LoadSubscript` currently has no such call/return
    shape, so Stage 2 must decide whether the spike uses a synchronous helper,
    reuses part of this machinery, or temporarily accepts a narrower replay
    path.
- Code object cache storage and lifetime:
  - `src/code_object.h::CodeObject` owns vectors for attribute, module-global,
    function-call, and keyword-call caches.
  - `src/code_object.cpp::CodeObject::dealloc` clears attribute and
    module-global caches and resets function-call caches.
  - `FunctionCallInlineCache` stores raw `Function *`, `CodeObject *`, and
    `ValidityCell *` pointers. A get-item cache that stores `Value` payloads
    must make GC/refcount visibility explicit rather than copying that raw
    pointer pattern blindly.
- Operand shape API:
  - `src/thread_state.h::shape_of_value` returns heap-object shapes or inline
    value shapes.
  - `src/thread_state.h::class_of_value` returns
    `shape_of_value(value)->get_class()`.
  - `src/virtual_machine.h::shape_for_inline_value` covers SMI, bool, None,
    NotImplemented, and Ellipsis inline values.
- Lookup validity API:
  - `src/class_object.h` exposes
    `current_mro_shape_and_contents_validity_cell()` and
    `get_or_create_mro_shape_and_contents_validity_cell()`.
  - `src/class_object.cpp` invalidates these cells on class contents and MRO
    shape changes, including attached child-MRO dependencies.
- Builtin container `__getitem__` status at Stage 1:
  - `src/str.cpp` already exposes `str.__getitem__` through
    `native_str_getitem`.
  - `src/list.cpp`, `src/tuple.cpp`, and `src/dict.cpp` expose several builtin
    methods, but do not currently expose `__getitem__`.
  - Existing native list/tuple/dict subscription behavior lives in
    `src/subscript.h`, `src/subscript.cpp`, and the container methods such as
    `List::get_item` / `Tuple::get_item` / `Dict::get_item`.

### Stage 1.5: Add Minimal Builtin `__getitem__` Methods

- [x] Pick the smallest builtin set needed to exercise the cache. Start with one
  exact container/key pair, preferably `list[smi]`, and add `tuple`, `str`, or
  `dict` only if the spike needs broader coverage.
- [x] Expose each selected builtin container's existing native subscription behavior
  through its `__getitem__` special method.
- [x] Do not move existing direct opcode fast paths behind the protocol cache yet.
  This spike can call builtin `__getitem__` through tests or cache-miss paths
  without completing the full migration to protocol-selected trusted handlers.
- [x] Do not add trusted-handler metadata for these methods in this stage.

Checkpoint:

- [x] `container.__getitem__(key)` and `container[key]` agree for the selected
  builtin cases.
- [x] The builtin method is visible to dunder-method lookup so the get-item IC can
  cache the lookup fact.

Status: done. The spike selected the full currently supported subscription
builtin set: `list`, `tuple`, `str`, `dict`, and `slotdict`. No
trusted-handler metadata was added. Later stages moved `LoadSubscript` onto the
protocol-selected path, so these methods are now the builtin subscription path
rather than only test affordances.

### Stage 2: Define The Cache Payload

- [x] Add a `GetItemIC`-style payload with:
  - method-read cache state for the selected `__getitem__` lookup;
  - cached container shape through the method-read cache;
  - cached key `ShapeKey`;
  - lookup validity cell through the method-read cache;
  - cached function/code/adaptation payload for replaying the selected
    `__getitem__`;
  - no arbitrary cached result value.
- [x] Use the repository's ownership model for stored values:
  - borrowed handles are fine for transient locals only;
  - code-object-owned cache state stores raw `Function *` / `CodeObject *`
    pointers in the same style as existing call caches, rather than storing
    heterogeneous `Value` payloads.
- [x] Keep the payload monomorphic and replacement-only.

Checkpoint:

- [x] The payload can represent "cacheable function-shaped method" and
  "uncacheable" without inventing descriptor-general behavior.

Status: done. `GetItemInlineCache` now uses an `AttributeReadInlineCache` for
the selected `__getitem__` lookup, a `ShapeKey` for the key guard, and a
getitem-specific call payload: `Function *`, `CodeObject *`, final argument
count, `FunctionCallAdaptation`, and whether the call has bound `self`. It
intentionally no longer embeds `FunctionCallInlineCache`: the special-method
read cache uses contents-sensitive validity, so the cached function payload is
guarded by the same lookup dependency.

### Stage 3: Thread A Cache Index Through Bytecode

- [x] Add a cache index operand to get-item bytecode forms that can invoke
  `__getitem__`.
- [x] Allocate the corresponding cache entry on the code object.
- [x] Update bytecode printing/scanning/source-offset logic as needed.
- [x] Keep ordinary control flow unchanged.

Checkpoint:

- [x] Existing get-item behavior is unchanged when the cache is empty or disabled.

Status: done in the first Stage 3 implementation, then adjusted for the Stage
4 call-entry shape. `LoadSubscript` now carries one `get_item_ic` operand
allocated from `CodeObject::get_item_caches`, and codegen lowers `obj[key]`
with a prepared call-frame-aligned argument span containing `[container, key]`.
Disassembly prints the cache operand next to the first argument register. The
interpreter uses that cache index for miss population and hit replay.

### Stage 4: Resolve And Execute An Uncached Get-Item Plan

The next slice should not merely record lookup facts. It should create the same
executable get-item call plan that a later cache hit can reuse, then execute
that plan immediately on the miss path.

This spike may let `LoadSubscript` itself enter the selected `__getitem__`
callee and treat the callee's accumulator return value as the final
subscription result. That shape is acceptable for get-item because
`NotImplemented` is just an ordinary return value and there is no reflected or
in-place protocol continuation to resume after the call. It should not be
treated as the final operator-dispatch architecture. Before trusted fast paths,
binary/reflected operators, or protocols that need post-call continuation, the
bytecode shape should be revisited so operands and protocol state live in frame
registers or explicit continuation state rather than relying on the
accumulator.

- [x] Move `LoadSubscript` away from the existing exact native-layout subscription
  fast path and toward protocol-selected `__getitem__` execution. The native
  helpers may still be reused behind builtin `__getitem__` method bodies, but
  the opcode should not bypass special-method lookup for the builtin container
  cases that now expose visible `__getitem__` methods.
- [x] Resolve `type(container).__getitem__` with
  `resolve_special_method_read_descriptor`.
- [x] Convert the descriptor into an executable method-call target:
  - selected callable;
  - optional bound `self`;
  - argument count including bound `self` when present;
  - function-call adaptation needed to enter the target frame.
- [x] Treat missing `__getitem__`, unsupported descriptors, unusual binding, class
  subscription, non-function callables, wrong arity, and lookup errors as
  conservative uncached failures for this spike.
- [x] Enter the selected function-shaped `__getitem__` through the existing
  positional call machinery, using the `LoadSubscript` instruction length as
  the return point.
- [x] Do not commit cache state merely because lookup produced interesting facts.
  Cache state should only be populated once lookup, binding, callable
  validation, and call adaptation have produced an executable plan.

Checkpoint:

- [x] Builtin and user-defined `obj[key]` calls flow through the protocol-selected
  `__getitem__` path for the cases supported by the spike.
- [x] Side effects in argument evaluation, lookup, binding, and the
  `__getitem__` body remain Python-visible and happen in the same order as the
  uncached path.
- [x] Missing or unsupported `__getitem__` preserves the current subscription error
  behavior.
- [x] If the selected `__getitem__` body raises, the exception propagates normally.
  The spike does not require continuation machinery to defer cache publication
  until after the method body returns.

Status: done. `LoadSubscript` no longer bypasses `__getitem__` for supported
builtin containers. The miss path is `op_load_subscript_cache_miss`, which
resolves `type(container).__getitem__`, prepares the selected managed
`Function` call from the `[container, key]` argument span, and installs cache
state only after lookup, binding, callable validation, and call adaptation have
produced an executable plan.

### Stage 5: Cache And Replay The Executable Plan

Once Stage 4 has a coherent executable miss path, make the cache entry hold
exactly the guards and call-adaptation state needed to replay that same plan.

- [x] Populate the monomorphic cache only after Stage 4 has produced a complete
  executable plan:
  - receiver-shape and validity guarded special-method read plan;
  - key-shape guard;
  - getitem-specific call payload for the selected function-shaped callable,
    final positional argument count, adaptation, and bound-self shape.
- [x] On hit, validate receiver shape, lookup validity, key shape, cached
  function payload, fixed-arity shape, and bound-self shape.
- [x] Replay the cached positional call from the cached
  `Function *` / `CodeObject *` / argument-count / adaptation payload. The hit
  path skips repeated `__getitem__` lookup only; it must still execute the
  Python-visible method body every time.
- [x] If any guard fails, fall back to the Stage 4 resolver and replace the
  monomorphic entry if the new operation is cacheable.
- [x] Do not cache negative lookup results in this spike unless that falls out of
  the same executable-plan representation without extra policy.

Checkpoint:

- [x] Cache hits skip repeated special-method lookup while still calling
  `__getitem__` on every execution.
- [x] Cache misses and guard failures use the same executable resolver as the
  uncached Stage 4 path.
- [x] A `__getitem__` body that raises may leave behind a valid lookup/call cache if
  lookup and call adaptation already succeeded; the cache still does not cache
  the exception or the result value.

Status: done for monomorphic function-shaped replay. The fixed-arity
`self == receiver` hit path is hot and protected by the opcode-frame checker.
Cached calls that need defaults, varargs packing, or non-self bound receiver
handling tail to `op_load_subscript_cached_call_slow`; cache misses and guard
failures use `op_load_subscript_cache_miss`. Trusted native handlers remain out
of scope.

### Stage 6: Invalidation And Mutation Tests

Add interpreter-level tests for:

- [x] repeated `obj[key]` calls on a user class hit the cached selected method
  without changing results
- [x] repeated `container[key]` calls for the selected builtin container exercise a
  real builtin `__getitem__` method rather than a parallel opcode-only path
- [x] replacing `C.__getitem__` after cache installation invalidates or misses and
  calls the new method
- [ ] adding `__getitem__` after a negative lookup does not keep using the old
  missing result if negative lookup caching is enabled
- [ ] inherited `__getitem__` invalidates when the defining base class changes
- [x] side effects inside `__getitem__` still run on every cached execution
- [x] `__getitem__` returning `NotImplemented` returns that singleton
- [ ] raising `__getitem__` propagates normally and does not cache the exception or
  result value
- [ ] different key shapes at the same bytecode site miss and replace the
  monomorphic entry

If negative lookup caching is not implemented in the spike, test that negative
lookups are simply left uncacheable.

Status: partial. Core hit/replacement/side-effect/NotImplemented/defaults and
initial key-shape storage are covered. Explicit inherited-base invalidation,
negative-lookup uncached behavior, raising-method behavior, and key-shape
replacement coverage are still open.

### Stage 7: Structural And Regression Tests

- [x] Add focused codegen or bytecode tests only for the new cache-index operand and
  cache allocation.
- [x] Prefer interpreter tests for Python-visible behavior.
- [x] Run `clang-format -i` on every touched C++ source or header.
- [x] Run `ninja -C build-debug all check`.

Checkpoint:

- [x] The spike can be evaluated without trusted handlers, continuation opcodes, or
  performance claims.

Status: mostly done. `ninja -C build-debug all check` was green after the code
changes, and focused release opcode-frame checks and getitem benchmarks were
run during the hot-path cleanup. This document-only update does not require a
new build.

## Blurry Areas The Spike Should Resolve

- [x] What exact shape API should operator dispatch use for inline values,
  builtin heap values, user instances, and class objects?

  Use `ShapeKey` as the opaque comparable cache key. Convert back to a real
  shape only when lookup/class information is needed.
- [x] Can the current special-method/function-call path replay a cached
  function-shaped `__getitem__` cleanly?

  Yes for managed function-shaped calls. The hot path handles fixed arity and
  bound `self == receiver`; defaults, varargs, and non-self bound receiver
  cases go through a dedicated cached-call slow path.
- [x] Is the transient miss-time executable plan the right boundary, or does
  `LoadSubscript` need a separate call-continuation opcode before trusted
  handlers or more complex descriptors?

  The executable-plan boundary is enough for getitem. Getitem has no reflected
  or continuation protocol after the selected call returns. The accumulator-free
  `[container, key]` call-argument layout should still be revisited before
  broader operator caches.
- [x] What ownership wrapper is required for cached callable values inside code
  objects?

  No arbitrary cached `Value` is stored. The cache stores raw
  `Function *` / `CodeObject *` pointers like existing call caches.
- [x] Does the existing MRO shape-and-contents validity cell cover all lookup
  dependencies needed by this first cache?

  Yes. Special-method lookup uses the contents-sensitive MRO validity cell, so
  class-content writes invalidate the getitem cache dependency.
- [x] Which descriptor or binding cases are common enough that treating them as
  uncacheable would make the first cache ineffective?

  Managed function-shaped methods are enough for the spike. Descriptor-general
  replay remains out of scope.
- [x] Does the bytecode cache-index plumbing fit the current code object and
  disassembly model cleanly?
- [x] How much builtin subscription behavior must be exposed as real
  `__getitem__` methods before the cache can test the intended
  protocol-selected path?

  The current supported builtin subscription set is exposed.

## Remaining Work

- [ ] Add explicit interpreter tests for inherited-base invalidation.
- [ ] Add explicit interpreter tests that negative lookups remain uncached.
- [ ] Add explicit interpreter tests for raising `__getitem__` after cache
  publication.
- [ ] Add explicit interpreter tests for key-shape miss and monomorphic
  replacement at the same bytecode site.
- [ ] Design trusted native getitem handler publication and cache payloads.
- [ ] Decide how much getitem register traffic to remove in codegen now that
  `LoadSubscript` no longer uses the accumulator as the key.

## Completion Criteria

The spike is successful if:

- [x] cache hits skip repeated `__getitem__` lookup for cacheable user-defined
  function-shaped methods
- [x] at least one builtin container used by tests exposes a real `__getitem__`
  method that agrees with existing subscription behavior
- [x] cached executions still call `__getitem__` every time
- [x] direct class mutation invalidates the cached lookup dependency
- [ ] base-class mutation invalidation has explicit test coverage
- [x] unsupported descriptor and class-subscription cases fail conservatively
- [x] the implementation identifies whether the executable-plan boundary is enough,
  or whether continuation machinery is actually needed before trusted handlers
  or binary operator caches

The spike is not required to show a speedup. A clean semantic cache with clear
miss behavior is enough.
