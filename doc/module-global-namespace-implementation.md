# Module Global Namespace Implementation Plan

This checklist implements the direction described in
[Module Global Namespace Design Sketch](module-global-namespace-design.md).

The migration strategy is to build the new module-object path beside the current
`Scope`-backed module/global implementation, test it directly, and switch codegen
only after the runtime path exists.

## Current State

Module globals currently live in `Scope` instances. Those scopes are present
during compilation, and codegen allocates stable slots directly in them. The
module global scope may also contain holes that cache builtin shadowing and
parent-scope lookup facts.

That gives the current bytecode a direct slot-based path, but it also makes the
compiler's symbol structure double as runtime module storage.

## Target Shape

These are design constraints, not checklist items:

- `ModuleObject` owns module identity and module global storage.
- Module global storage is shape-backed from the start of the new path.
- `__name__` and `__builtins__` are predefined module slots, in that order, so
  C++ runtime code has fixed locations for both.
- Module/global lookup uses explicit runtime state: defining module first,
  resolved builtins second.
- Builtin fallback is a lookup rule, not a value copied into the module.
- `LOAD_GLOBAL`-style caches guard lookup location, not loaded value identity.
- `Scope` remains compiler/name-binding metadata: name to stable slot identity,
  plus ordered name/slot metadata when dictionary-like presentation is required.
- The end state has no scope-owned module values and no parent-slot fallback
  payloads in `Scope`.

## Validity Cells

Validity cells are not part of the first `ModuleObject` layout. The first
module object should mirror the `ClassObject` slot-layout trick: fixed metadata
and predefined slots first, then a large inline slot area. Later validity state
can be added deliberately once the cache design is clearer.

Expected later validity state:

- module shape or module namespace membership validity for assumptions that a
  global name is present or absent in the module
- builtins shape or builtins namespace membership validity for assumptions that a
  builtin name is present or absent
- dedicated module `__builtins__` binding validity, because assigning that
  predefined slot can change resolved builtins without changing module shape
- storage-location validity only if the chosen storage location can move without
  a shape/membership change

Shape guards may serve as membership validity where module storage is
shape-backed and every membership change transitions shape.

## Stage 0: Map The Current Scope World

- [x] Inventory every `CodeObject::module_scope` use.
- [x] Inventory every codegen path that allocates module/global `Scope` slots.
- [x] Inventory where codegen creates holes in module scope for builtin
      shadowing or parent lookup.
- [x] Inventory interpreter handlers that read module globals by scope slot.
- [x] Inventory interpreter handlers that write or delete module globals by scope
      slot.
- [x] Inventory tests that directly inspect `module_scope`.
- [x] Inventory startup, REPL, and interactive paths that create or reuse a
      module/global `Scope`.
- [x] Identify the smallest test hooks needed to exercise module-object global
      storage before codegen emits the new instructions.

### Stage 0 Notes

Current ownership:

- `CodeObject` owns `MemberHeapPtr<Scope> module_scope`.
- `ThreadState::compile` creates a fresh `Scope` parented to
  `VirtualMachine::builtin_scope_ptr()`.
- `ThreadState::compile_in_scope` and the REPL reuse a caller-provided
  `module_scope` for interactive persistence.
- `codegen_module` creates a fresh module `Scope`; `codegen_module_in_scope`
  threads an existing one into `CodeObjectBuilder`.
- Function and class codegen receive the surrounding `module_scope`, so nested
  code objects keep using the defining module scope.

Current codegen path:

- Scope analysis allocates global read slots with
  `Scope::register_slot_index_for_read`.
- Scope analysis allocates global write/delete slots with
  `Scope::register_slot_index_for_write`.
- Global variable reads emit `LdaGlobal [slot]`.
- Global variable writes emit `StaGlobal [slot]`.
- Global variable deletes emit `DelGlobal [slot]`.
- Trusted Clover builtin-class constants still consult
  `VirtualMachine::builtin_scope_ptr()` directly during codegen.

Current builtin-shadowing hole:

- `Scope::register_slot_index_for_read` recursively registers the name in the
  parent scope.
- The module slot value is initialized as `Value::not_present(parent_slot_idx)`.
- `Scope::get_by_slot_index_fastpath_only` follows that encoded parent slot for
  builtin fallback.
- `Scope::register_slot_index_for_write` initializes
  `Value::not_present(-1)`, so stores and deletes target only the module scope.

Current interpreter path:

- `op_lda_global` reads `code_object->module_scope` by slot index and uses the
  parent-slot hole for builtin fallback.
- `op_sta_global` writes `code_object->module_scope` by slot index and uses the
  slow path when reviving a deleted named slot.
- `op_del_global` checks module slot liveness and writes `Value::not_present()`;
  it does not delete from the builtin parent scope.
- Error formatting recovers global names from
  `module_scope->get_name_by_slot_index`.

Current tests:

- Codegen tests assert `LdaGlobal`, `StaGlobal`, and `DelGlobal` slot operands.
- `Interpreter.builtin_scope_lookup` asserts that a module global slot can hold a
  builtin fallback value through the parent-slot fast path.
- `Interpreter.global_delete_does_not_delete_builtin_fallback` and
  `Interpreter.global_delete_reveals_builtin_after_shadow_delete` cover delete
  behavior around builtin fallback.
- `Interpreter.interactive_assignment_returns_none_and_persists_scope` covers
  interactive persistence through a shared module `Scope`.
- Many interpreter and attribute tests inspect `code_obj->module_scope`
  directly to fetch top-level definitions or inject globals.
- There is no dedicated `__builtins__` behavior test yet.
- There is no dedicated cross-module caller/defining-module test yet.

First test hooks for the new path:

- C++ helper tests for constructing a shape-backed `ModuleObject`.
- C++ helper tests for ordinary module property get/set/delete.
- C++ helper tests for predefined `__name__` and `__builtins__`
  get/set/delete.
- Direct interpreter or bytecode-builder tests for new module-global opcodes
  before ordinary codegen switches over.

## Stage 1: Minimal Shape-Backed ModuleObject

- [x] Add a `ModuleObject` heap type.
- [x] Give `ModuleObject` ordinary object identity.
- [x] Add builtin type/class bootstrap support for module objects if required by
      the current object model.
- [x] Give module objects shape-backed own-property storage.
- [x] Use a `ClassObject`-style layout with C++ fields stored before the inline
      slot area.
- [x] Reserve 256 inline slots for module object storage.
- [x] Reserve predefined slot 0 for `__name__`.
- [x] Reserve predefined slot 1 for `__builtins__`.
- [x] Add C++ accessors for the predefined `__name__` slot.
- [x] Add C++ accessors for the predefined `__builtins__` slot.
- [x] Add construction helpers on `ThreadState`.
- [x] Create a startup module object.
- [x] Create or reuse a shared interactive module object.
- [x] Keep current `Scope`-backed global execution unchanged while module objects
      are introduced.
- [x] Add tests for constructing a module object.
- [x] Add tests for reading and writing ordinary shape-backed module properties
      through C++ helpers.
- [x] Add tests for reading and writing the predefined `__name__` slot through
      C++ helpers.
- [x] Add tests for reading and writing the predefined `__builtins__` slot
      through C++ helpers.
- [x] Add tests that the first ordinary module global does not overlap the
      predefined slots.

## Stage 2: Module Shape Mutation And Validity

- [x] Use ordinary shape `StorageLocation` descriptors for module globals.
- [x] Add module shape guards for global lookup assumptions.
- [x] Add module membership validity cells if shape guards alone are not enough
      for the chosen storage representation.
- [x] Add module-as-builtins dependency attachment for module builtins fallback
      assumptions.
- [x] Design where dedicated `__builtins__` binding validity lives without
      aliasing module inline slots.
- [x] Add dedicated `__builtins__` binding validity once the layout is explicit.
- [x] Invalidate module miss assumptions when a module property is inserted.
- [x] Invalidate module hit assumptions when a module property is deleted.
- [x] Invalidate attached module builtins fallback assumptions when a module used
      as builtins mutates.
- [x] Invalidate resolved-builtins assumptions when the module `__builtins__`
      binding helper inserts, assigns, or deletes the binding.
- [x] Add focused tests or C++ tests that observe validity invalidation on module
      insert.
- [x] Add focused tests or C++ tests that observe validity invalidation on module
      delete.
- [x] Add focused tests or C++ tests that observe `__builtins__` binding
      invalidation on assignment without a module shape change.

## Stage 3: Runtime Helpers For Module Global Access

- [x] Add descriptor/result types that separate the selected action, cacheability,
      and non-cache metadata.
- [x] Add a helper for module-global lookup by name.
- [x] Add a helper for module-global store by name.
- [x] Add a helper for module-global delete by name.
- [x] Add a module-object helper for resolving module-valued builtins and the
      validity cell protecting that resolution.
- [x] Add fallback to VM default builtins when the module `__builtins__` binding
      is missing.
- [x] Add module-valued `__builtins__` handling.
- [x] Treat non-module `__builtins__` as an explicit uncacheable builtins object;
      do not traverse it in Stage 3.
- [x] Wire module-valued builtins resolution to attach the consumer module's
      builtins validity cell to the provider module exactly when the cell is
      created.
- [x] Add a helper for `LOAD_GLOBAL` semantics: module lookup, then resolved
      module-valued builtins lookup.
- [x] Add tests for module hit lookup through the helper.
- [x] Add tests for builtin hit after module miss through the helper.
- [x] Add tests for missing name through the helper.
- [x] Add tests that repeated module-valued builtins lookup does not attach
      duplicate validity cells to the provider module.
- [x] Add tests that deleting a module binding reveals a builtin without mutating
      the builtins namespace.
- [x] Add tests for assigning and deleting module `__builtins__` while it remains
      read-only through normal module-global store/delete helpers.

## Stage 4: Module Context In Code Objects, Functions, And Frames

- [x] Add an explicit defining-module reference to `CodeObject` or its successor
      module-context holder.
- [x] Move the transitional legacy module scope onto `ModuleObject` instead of
      keeping duplicate module context on `CodeObject`.
- [x] Route old `Scope`-slot global bytecode through
      `CodeObject::defining_module` and the module object's legacy scope.
- [x] Keep managed `Function` objects deriving their module context from their
      code object's defining module.
- [x] Use late builtins resolution from the defining module for now; do not add
      a resolved-builtins field to `Function` or frame state in this stage.
- [x] Thread `ModuleObject` through module code-object construction.
- [x] Thread `ModuleObject` through function code-object construction.
- [x] Thread `ModuleObject` through class code-object construction.
- [x] Thread `ModuleObject` through native-to-managed entry helpers.
- [x] Thread the shared interactive module object through REPL compilation.
- [x] Treat the module-owned legacy scope as the migration bridge:
      the legacy scope serves old instructions, and `defining_module` serves new
      helpers, instructions, and caches.
- [x] Preserve one shared interactive module object during REPL compilation.

## Stage 5: New Module Global Instructions

- [x] Add a module-global load instruction that uses the defining module and
      builtins runtime state.
- [x] Add a module-global store instruction that writes only to the defining
      module object.
- [x] Add a module-global delete instruction that deletes only from the defining
      module object.
- [x] Decide whether instruction operands are interned-name IDs, symbol slot IDs,
      or cache indices plus name IDs.
- [x] Keep the old `Scope`-slot global instructions in place during this stage.
- [x] Add interpreter handlers for the new module global instructions.
- [x] Keep handler slow paths explicit and cold where they format errors or raise.
- [x] Add direct bytecode/codegen-test helpers that can emit the new instructions
      before ordinary codegen switches over.
- [x] Add interpreter tests for new-instruction module hit, builtin hit, missing
      name, store, and delete.
- [x] Add tests that new-instruction `DEL_GLOBAL` never deletes from builtins.

## Stage 6: Inline Caches For New Global Instructions

- [x] Add side-array cache storage for the new module-global load instruction.
- [x] Add side-array cache storage for the new module-global store instruction.
- [x] Add an uninitialized cache state.
- [x] Add a module-hit cache state.
- [x] Add a builtin-hit-through-module-miss cache state.
- [x] Keep missing-in-both uncached; missing immediately raises `NameError` and
      should not allocate cells for repeated failed lookups.
- [x] Guard cached slot plans with a single plan-owned validity cell.
- [x] Keep delete uncached; delete plans intentionally carry no validity cell.
- [x] Guard caches that depend on resolved builtins with module `__builtins__`
      binding validity through the plan-owned validity cell.
- [x] Keep dict-like or arbitrary builtins mappings out of the first cacheable
      module-global path; add them only after their lookup semantics and
      invalidation rules are designed separately.
- [x] Do not add storage-location validity guards; current module storage
      locations are guarded by the plan-owned validity cell.
- [x] Add tests that cache entries observe module rebinding.
- [x] Add tests that cache entries observe module deletion revealing builtins.
- [x] Add tests that cache entries observe builtins mutation.
- [x] Add tests that cache entries observe `__builtins__` reassignment.

## Stage 7: Switch Codegen To ModuleObject Globals

- [x] Stop allocating module/global runtime value slots in `Scope` during
      codegen.
- [x] Keep allocating symbol metadata and ordered name/slot metadata in `Scope`
      where codegen still needs it.
- [x] Stop creating module-scope holes for builtin shadowing in generated
      module-global bytecode.
- [x] Emit the new module-global load instruction for global reads.
- [x] Emit the new module-global store instruction for global writes.
- [x] Emit the new module-global delete instruction for global deletes.
- [x] Update error/debug metadata so global names are recoverable without
      scope-owned runtime values.
- [x] Update function creation so functions capture or receive the chosen
      defining-module and builtins context.
- [x] Update class codegen enough to preserve current behavior while global
      lookup moves to module objects.
- [x] Update the tests broken by the codegen switch to inspect module object
      storage or the defining module instead of the legacy module scope.
- [x] Update codegen golden tests for module-global opcodes, name constants, and
      cache operands.
- [x] Preserve trusted builtin initialization in release builds; do not hide
      module-storage side effects inside `assert(...)`.
- [x] Run the interpreter semantic tests for globals, builtins, functions, and
      interactive execution.
- [x] Run release benchmarks after the codegen switch.
- [x] Profile module-global read/write regressions and decide whether they belong
      in this stage or in the next cache/performance pass.
- [x] Accept the current module-global add/delete regression as a consequence of
      real shape transitions unless profiling shows it affects hot workloads.

### Stage 7 Notes

Slice 1 has landed: ordinary global reads, writes, and deletes now emit
`LdaModuleGlobal`, `StaModuleGlobal`, and `DelModuleGlobal`. The new bytecode
instructions carry name constants, so global read/delete `NameError` reporting
no longer depends on scope-owned runtime slots. The legacy `LdaGlobal`,
`StaGlobal`, and `DelGlobal` bytecodes have now been removed.

Nested functions and class bodies now receive the defining module from their
enclosing code object. Function globals therefore resolve through the defining
module. Class bodies still use local `Scope` storage for class-local execution
and class namespace harvesting, but their global lookup path is also
module-object based.

Profiling after the switch showed the expected cold-path cost for global
add/delete, because module deletes now perform real shape transitions and
invalidate module lookup state instead of writing `not_present` into an existing
scope slot. We accept that add/delete regression for now. Hot read/write
profiling led to two cleanup fixes: module-global refcounted stores now avoid
thread-local zero-count lookup on the hot path, and module-global plans are split
from cache-entry structs so cache payloads carry only cacheable hit state.

Stage 7 intentionally leaves the legacy `Scope` bridge in place. Stage 8 owns
removing scope-backed module runtime value cells, builtin-shadowing holes, and
the remaining compatibility paths while preserving whatever symbol metadata
codegen still needs.

Follow-up cleanup removed the last generated-code dependency on module
`Scope` slot allocation: global access analysis now marks global reads, writes,
and deletes as name-based accesses without registering legacy module slots. This
means generated module-global bytecode no longer creates parent-slot builtin
fallback holes. Local and class-body scopes still allocate the local slot
metadata they need for frame storage and class namespace harvesting.

## Stage 8: Retire Module Values From Scope

- [x] Remove interpreter use of `Scope` APIs for module runtime value lookup.
- [x] Remove interpreter use of `Scope` APIs for module runtime stores.
- [x] Remove interpreter use of `Scope` APIs for module runtime deletes.
- [x] Remove parent-slot encoding for module/builtin lookup from `Scope`.
- [x] Remove scope-owned runtime value cells once no remaining non-module user
      depends on them.
- [x] Preserve `Scope` name-to-slot metadata.
- [x] Preserve `Scope` insertion-order metadata for dictionary-like presentation
      when needed.
- [ ] Replace Stage 3 semantic C++ helper coverage with Python-level interpreter
      tests once codegen emits module-global instructions; keep only low-level
      descriptor/validity helper tests that are not observable from Python.
- [x] Rename remaining `module_scope` fields and helpers to semantic names.
- [x] Delete transitional compatibility paths.

### Stage 8 Notes

The legacy slot-index global bytecodes are gone. `LdaGlobal`, `StaGlobal`, and
`DelGlobal` no longer exist in the bytecode enum, builder API, disassembler, or
interpreter dispatch table, so interpreter module-global execution now goes
through the module-object opcodes exclusively. Codegen also no longer eagerly
creates a legacy module scope for generated modules, functions, or classes.

The module-owned legacy scope bridge has been removed. `ModuleObject` no longer
stores a legacy scope pointer, `CodeObject` no longer exposes a legacy module
scope accessor, and the transitional `ThreadState::compile_in_scope` entrypoint
has been deleted. The remaining `Scope` uses are local/class frame metadata.

Builtin initialization now writes directly into the global builtins module.
`VirtualMachine::initialize_builtin_scope` has become `initialize_builtins`, the
temporary builtin `Scope` mirror and copy-back pass are gone, and builtin class
lookups read the builtins module instead of `Scope`.

`Scope` no longer encodes parent-scope fallback slots in `not_present` values.
`register_slot_index_for_read` now allocates the same local missing slot as
`register_slot_index_for_write`, and `get_by_name` / `get_by_slot_index` report
missing local values directly instead of walking parent scopes.

`Scope` no longer owns runtime value cells. Function locals and class bodies use
frame slots for runtime storage; `Scope` now keeps only parent-scope identity,
name-to-slot lookup, slot-name metadata, and insertion-order entries. Class body
frame initialization writes `not_present` directly into named frame slots, and
the old indexed `Value::not_present(parent_slot_idx)` encoding has been deleted.

## Stage 9: Module Attributes And Mapping Views

- [ ] Route `module.x` reads through module object attribute semantics over the
      shape-backed module storage.
- [ ] Route `module.x = value` through module object attribute semantics over the
      shape-backed module storage.
- [ ] Route `del module.x` through module object attribute semantics over the
      shape-backed module storage.
- [ ] Keep `LOAD_GLOBAL` separate from module attribute lookup.
- [ ] Implement `globals()` as a live mutable view over module object storage.
- [ ] Implement module-scope `locals()` as the same live mutable view.
- [ ] Decide whether `module.__dict__` lands here or remains a follow-up.
- [ ] Add tests that top-level stores, `globals()["x"]`, and `module.x` observe
      the same own module binding where supported.
- [ ] Add tests that builtin fallback does not appear as a module own attribute
      or `module.__dict__` entry.

## Stage 10: Import And Multi-Module Execution

- [ ] Add tests that functions use their defining module, not the caller's
      module, for global lookup.
- [ ] Add dict-like `__builtins__` traversal later, after dict namespace
      semantics and cache invalidation have a dedicated design.

## Stage 11: Class Namespace Follow-Up

- [ ] Add `active_locals_namespace` to frame state.
- [ ] Add `LOAD_NAME`.
- [ ] Add `STORE_NAME`.
- [ ] Add `DEL_NAME`.
- [ ] Compile class body name access to name opcodes.
- [ ] Create a temporary class namespace before class body execution.
- [ ] Make `BuildClass` consume the active class namespace.
- [ ] Make class-body `locals()` return the active class namespace.
- [ ] Preserve the current class local-slot harvest path until this stage is
      complete.
- [ ] Add tests that class body locals are visible in class construction.
- [ ] Add tests that nested methods do not capture ordinary class-body names as
      closure locals.

## Stage 12: Verification And Cleanup

- [ ] Update docs that mention module globals as `Scope` storage.
- [ ] Update architecture references if new module/object docs were added during
      implementation.
- [ ] For performance-sensitive cache or interpreter changes, run release
      benchmarks or relevant targeted benchmarks.
