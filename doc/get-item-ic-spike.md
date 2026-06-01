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

On a cache miss, the opcode should run the normal get-item behavior, record the
lookup facts if they are cacheable, and install or replace the monomorphic
entry only after the operation completes successfully.

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

- Find the current `LoadSubscript` bytecode definition, encoder/decoder, and
  interpreter handler.
- Find the current generic get-item or subscription helper.
- Find the current special-method call path used for function-shaped dunder
  methods.
- Find the existing inline-cache storage pattern on code objects.
- Find the existing validity-cell API for class MRO shape and contents.
- Identify how code objects expose cache payloads to GC tracing.
- Identify which builtin containers already have native subscription helpers
  but do not expose `__getitem__` as a normal special method.

Checkpoint:

- Write down the exact files and helper functions the spike will touch.
- Do not change bytecode format until the cache payload and call path are clear.

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

- Pick the smallest builtin set needed to exercise the cache. Start with one
  exact container/key pair, preferably `list[smi]`, and add `tuple`, `str`, or
  `dict` only if the spike needs broader coverage.
- Expose each selected builtin container's existing native subscription behavior
  through its `__getitem__` special method.
- Do not move existing direct opcode fast paths behind the protocol cache yet.
  This spike can call builtin `__getitem__` through tests or cache-miss paths
  without completing the full migration to protocol-selected trusted handlers.
- Do not add trusted-handler metadata for these methods in this stage.

Checkpoint:

- `container.__getitem__(key)` and `container[key]` agree for the selected
  builtin cases.
- The builtin method is visible to dunder-method lookup so the get-item IC can
  cache the lookup fact.

Status: done in `6e3a5d90`. The spike selected the full currently supported
subscription builtin set: `list`, `tuple`, `str`, `dict`, and `slotdict`.
Existing direct subscript fast paths remain in place, and no trusted-handler
metadata was added.

### Stage 2: Define The Cache Payload

- Add a `GetItemIC`-style payload with:
  - occupied/empty state
  - cached container shape
  - cached key shape
  - lookup validity cell
  - cached callable or replay plan for the selected `__getitem__`
  - any existing function-call cache payload needed for the replay call
- Use the repository's ownership model for stored values:
  - borrowed handles are fine for transient locals only
  - code-object-owned cached values must be represented so GC can observe them
- Keep the payload monomorphic and replacement-only.

Checkpoint:

- The payload can represent "cacheable function-shaped method" and
  "uncacheable" without inventing descriptor-general behavior.

Status: done in the first Stage 2 implementation. The spike uses one unified
`GetItemInlineCache` entry on `CodeObject`, with an embedded
`AttributeReadInlineCache` for the selected `__getitem__` lookup, an explicit
key-shape guard, and an embedded `FunctionCallInlineCache` for replaying the
selected function-shaped method call. This keeps get-item caches in their own
code-object table while reusing the existing attribute-read and function-call
cache payloads internally. The entry does not store an arbitrary callable
`Value`, so Stage 2 does not add new code-object GC/refcount tracing
requirements.

### Stage 3: Thread A Cache Index Through Bytecode

- Add a cache index operand to get-item bytecode forms that can invoke
  `__getitem__`.
- Allocate the corresponding cache entry on the code object.
- Update bytecode printing/scanning/source-offset logic as needed.
- Keep ordinary control flow unchanged.

Checkpoint:

- Existing get-item behavior is unchanged when the cache is empty or disabled.

Status: done in the first Stage 3 implementation. `LoadSubscript` now carries
one `get_item_ic` operand allocated from `CodeObject::get_item_caches`.
Disassembly prints the new cache operand, and the interpreter reads but does
not use the cache index yet. Runtime behavior remains on the existing
`load_subscript` helper.

### Stage 4: Implement Miss-Time Lookup Recording

- On miss, perform the same dunder-method lookup the generic get-item path uses.
- Record:
  - receiver class searched
  - found/missing/uncacheable/error status
  - selected function-shaped method, if found
  - lookup validity cell
  - binding behavior needed to call the selected method
- Treat unsupported descriptors, unusual binding, class subscription, and lookup
  errors as uncacheable for this spike.
- Install a cache entry only after the subscription operation returns normally.

Checkpoint:

- A failed or raising `__getitem__` call does not install an entry.
- A missing or unsupported `__getitem__` path preserves the current error
  behavior.

### Stage 5: Implement Hit-Time Validation And Replay

- On hit, validate container shape, key shape, and lookup validity.
- If any guard fails, run the miss path.
- If all guards pass, call the cached selected `__getitem__` method using the
  existing call machinery.
- Propagate pending exceptions exactly like the uncached path.
- Return any non-exception result directly, including `NotImplemented`.

Checkpoint:

- The hit path skips lookup only. It does not skip the Python-visible method
  call.

### Stage 6: Invalidation And Mutation Tests

Add interpreter-level tests for:

- repeated `obj[key]` calls on a user class hit the cached selected method
  without changing results
- repeated `container[key]` calls for the selected builtin container exercise a
  real builtin `__getitem__` method rather than a parallel opcode-only path
- replacing `C.__getitem__` after cache installation invalidates or misses and
  calls the new method
- adding `__getitem__` after a negative lookup does not keep using the old
  missing result if negative lookup caching is enabled
- inherited `__getitem__` invalidates when the defining base class changes
- side effects inside `__getitem__` still run on every cached execution
- `__getitem__` returning `NotImplemented` returns that singleton
- raising `__getitem__` propagates and does not install an entry
- different key shapes at the same bytecode site miss and replace the
  monomorphic entry

If negative lookup caching is not implemented in the spike, test that negative
lookups are simply left uncacheable.

### Stage 7: Structural And Regression Tests

- Add focused codegen or bytecode tests only for the new cache-index operand and
  cache allocation.
- Prefer interpreter tests for Python-visible behavior.
- Run `clang-format -i` on every touched C++ source or header.
- Run `ninja -C build-debug all check`.

Checkpoint:

- The spike can be evaluated without trusted handlers, continuation opcodes, or
  performance claims.

## Blurry Areas The Spike Should Resolve

- What exact shape API should operator dispatch use for inline values, builtin
  heap values, user instances, and class objects?
- Can the current special-method/function-call path replay a cached
  function-shaped `__getitem__` cleanly?
- What ownership wrapper is required for cached callable values inside code
  objects?
- Does the existing MRO shape-and-contents validity cell cover all lookup
  dependencies needed by this first cache?
- Which descriptor or binding cases are common enough that treating them as
  uncacheable would make the first cache ineffective?
- Does the bytecode cache-index plumbing fit the current code object and
  disassembly model cleanly?
- How much builtin subscription behavior must be exposed as real `__getitem__`
  methods before the cache can test the intended protocol-selected path?

## Completion Criteria

The spike is successful if:

- cache hits skip repeated `__getitem__` lookup for cacheable user-defined
  function-shaped methods
- at least one builtin container used by tests exposes a real `__getitem__`
  method that agrees with existing subscription behavior
- cached executions still call `__getitem__` every time
- class or base-class mutation invalidates the cached lookup dependency
- unsupported descriptor and class-subscription cases fail conservatively
- the implementation identifies whether continuation machinery is actually
  needed before trusted handlers or binary operator caches

The spike is not required to show a speedup. A clean semantic cache with clear
miss behavior is enough.
