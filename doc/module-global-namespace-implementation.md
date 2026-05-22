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
- `__builtins__` is a predefined module slot so C++ runtime code has a fixed
  place to resolve the hidden frame builtins namespace.
- Module/global lookup uses explicit runtime state: defining module first,
  resolved builtins second.
- Builtin fallback is a lookup rule, not a value copied into the module.
- `LOAD_GLOBAL`-style caches guard lookup location, not loaded value identity.
- `Scope` remains compiler/name-binding metadata: name to stable slot identity,
  plus ordered name/slot metadata when dictionary-like presentation is required.
- The end state has no scope-owned module values and no parent-slot fallback
  payloads in `Scope`.

## Validity Cells

Validity cells should be introduced before the final codegen switch, even if the
first tests exercise them through focused C++ or interpreter hooks rather than a
full optimized path.

Required validity state:

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

- [ ] Inventory every `CodeObject::module_scope` use.
- [ ] Inventory every codegen path that allocates module/global `Scope` slots.
- [ ] Inventory where codegen creates holes in module scope for builtin
      shadowing or parent lookup.
- [ ] Inventory interpreter handlers that read module globals by scope slot.
- [ ] Inventory interpreter handlers that write or delete module globals by scope
      slot.
- [ ] Inventory tests that directly inspect `module_scope`.
- [ ] Inventory startup, REPL, and interactive paths that create or reuse a
      module/global `Scope`.
- [ ] Identify the smallest test hooks needed to exercise module-object global
      storage before codegen emits the new instructions.

## Stage 1: Minimal Shape-Backed ModuleObject

- [ ] Add a `ModuleObject` heap type.
- [ ] Give `ModuleObject` ordinary object identity.
- [ ] Add builtin type/class bootstrap support for module objects if required by
      the current object model.
- [ ] Give module objects shape-backed own-property storage.
- [ ] Reserve a predefined `__builtins__` slot on every module object.
- [ ] Add C++ accessors for the predefined `__builtins__` slot.
- [ ] Add module name storage.
- [ ] Add construction helpers on `ThreadState`.
- [ ] Create a startup module object.
- [ ] Create or reuse a shared interactive module object.
- [ ] Keep current `Scope`-backed global execution unchanged while module objects
      are introduced.
- [ ] Add tests for constructing a module object.
- [ ] Add tests for reading and writing ordinary shape-backed module properties
      through C++ helpers.
- [ ] Add tests for reading and writing the predefined `__builtins__` slot
      through C++ helpers.

## Stage 2: Module Shape Mutation And Validity

- [ ] Define the module global storage-location descriptor used by runtime
      helpers and future caches.
- [ ] Add module shape guards for global lookup assumptions.
- [ ] Add module membership validity cells if shape guards alone are not enough
      for the chosen storage representation.
- [ ] Add builtins namespace shape guards or membership validity cells.
- [ ] Add dedicated `__builtins__` binding validity to `ModuleObject`.
- [ ] Invalidate module miss assumptions when a module property is inserted.
- [ ] Invalidate module hit assumptions when a module property is deleted.
- [ ] Invalidate builtins hit/miss assumptions when the builtins namespace
      mutates.
- [ ] Invalidate resolved-builtins assumptions when module `__builtins__` is
      inserted, assigned, or deleted.
- [ ] Add focused tests or C++ tests that observe validity invalidation on module
      insert.
- [ ] Add focused tests or C++ tests that observe validity invalidation on module
      delete.
- [ ] Add focused tests or C++ tests that observe `__builtins__` binding
      invalidation on assignment without a module shape change.

## Stage 3: Runtime Helpers For Module Global Access

- [ ] Add a helper for module-global lookup by interned name.
- [ ] Add a helper for module-global store by interned name.
- [ ] Add a helper for module-global delete by interned name.
- [ ] Add a helper for resolving hidden frame builtins from a module object.
- [ ] Add fallback to VM default builtins when the module `__builtins__` binding
      is missing.
- [ ] Add module-valued `__builtins__` handling.
- [ ] Add dict-like `__builtins__` handling.
- [ ] Preserve non-module, non-dict-like `__builtins__` behavior so later builtin
      lookup fails through that object rather than silently ignoring it.
- [ ] Add a helper for `LOAD_GLOBAL` semantics: module lookup, then resolved
      builtins lookup.
- [ ] Add tests for module hit lookup through the helper.
- [ ] Add tests for builtin hit after module miss through the helper.
- [ ] Add tests for missing name through the helper.
- [ ] Add tests that deleting a module binding reveals a builtin without mutating
      the builtins namespace.
- [ ] Add tests for assigning and deleting module `__builtins__`.

## Stage 4: Module Context In Code Objects, Functions, And Frames

- [ ] Add an explicit defining-module reference to `CodeObject` or its successor
      module-context holder.
- [ ] Add an explicit defining-module reference to managed `Function` objects.
- [ ] Decide whether resolved builtins are captured by functions or resolved into
      frames at entry.
- [ ] Add the chosen resolved-builtins field to `Function` or frame setup state.
- [ ] Thread `ModuleObject` through module code-object construction.
- [ ] Thread `ModuleObject` through function code-object construction.
- [ ] Thread `ModuleObject` through class code-object construction.
- [ ] Thread `ModuleObject` through native-to-managed entry helpers.
- [ ] Thread the shared interactive module object through REPL compilation.
- [ ] Keep `module_scope` compatibility fields only as a bridge while old
      bytecode still needs them.
- [ ] Add tests that functions use their defining module, not the caller's
      module, for global lookup.
- [ ] Add tests that interactive compilation preserves one shared module object.

## Stage 5: New Module Global Instructions

- [ ] Add a module-global load instruction that uses the defining module and
      builtins runtime state.
- [ ] Add a module-global store instruction that writes only to the defining
      module object.
- [ ] Add a module-global delete instruction that deletes only from the defining
      module object.
- [ ] Decide whether instruction operands are interned-name IDs, symbol slot IDs,
      or cache indices plus name IDs.
- [ ] Keep the old `Scope`-slot global instructions in place during this stage.
- [ ] Add interpreter handlers for the new module global instructions.
- [ ] Keep handler slow paths explicit and cold where they format errors or raise.
- [ ] Add direct bytecode/codegen-test helpers that can emit the new instructions
      before ordinary codegen switches over.
- [ ] Add interpreter tests for new-instruction module hit, builtin hit, missing
      name, store, and delete.
- [ ] Add tests that new-instruction `DEL_GLOBAL` never deletes from builtins.

## Stage 6: Inline Caches For New Global Instructions

- [ ] Add side-array cache storage for the new module-global load instruction.
- [ ] Add an uninitialized cache state.
- [ ] Add a module-hit cache state.
- [ ] Add a builtin-hit-through-module-miss cache state.
- [ ] Add a missing-in-both cache state.
- [ ] Guard module-hit caches with module shape/membership validity and storage
      location.
- [ ] Guard builtin-hit caches with module miss validity plus builtins
      shape/membership validity and storage location.
- [ ] Guard missing caches with module miss validity plus builtins miss validity.
- [ ] Guard caches that depend on resolved builtins with module `__builtins__`
      binding validity.
- [ ] Add storage-location validity guards only if the location can move without
      shape or membership invalidation.
- [ ] Add tests that cache entries observe module rebinding.
- [ ] Add tests that cache entries observe module deletion revealing builtins.
- [ ] Add tests that cache entries observe builtins mutation.
- [ ] Add tests that cache entries observe `__builtins__` reassignment.

## Stage 7: Switch Codegen To ModuleObject Globals

- [ ] Stop allocating module/global runtime value slots in `Scope` during
      codegen.
- [ ] Keep allocating symbol metadata and ordered name/slot metadata in `Scope`
      where codegen still needs it.
- [ ] Stop creating module-scope holes for builtin shadowing.
- [ ] Emit the new module-global load instruction for global reads.
- [ ] Emit the new module-global store instruction for global writes.
- [ ] Emit the new module-global delete instruction for global deletes.
- [ ] Update error/debug metadata so global names are recoverable without
      scope-owned runtime values.
- [ ] Update function creation so functions capture or receive the chosen
      defining-module and builtins context.
- [ ] Update class codegen enough to preserve current behavior while global
      lookup moves to module objects.
- [ ] Update tests that inspect `module_scope` to inspect module object storage
      or the defining module instead.
- [ ] Run the interpreter semantic tests for globals, builtins, functions, and
      interactive execution.

## Stage 8: Retire Module Values From Scope

- [ ] Remove interpreter use of `Scope` APIs for module runtime value lookup.
- [ ] Remove interpreter use of `Scope` APIs for module runtime stores.
- [ ] Remove interpreter use of `Scope` APIs for module runtime deletes.
- [ ] Remove parent-slot encoding for module/builtin lookup from `Scope`.
- [ ] Remove scope-owned runtime value cells once no remaining non-module user
      depends on them.
- [ ] Preserve `Scope` name-to-slot metadata.
- [ ] Preserve `Scope` insertion-order metadata for dictionary-like presentation
      when needed.
- [ ] Rename remaining `module_scope` fields and helpers to semantic names.
- [ ] Delete transitional compatibility paths.

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

## Stage 10: Class Namespace Follow-Up

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

## Stage 11: Verification And Cleanup

- [ ] Update docs that mention module globals as `Scope` storage.
- [ ] Update architecture references if new module/object docs were added during
      implementation.
- [ ] Run `ninja -C build-debug all check`.
- [ ] For performance-sensitive cache or interpreter changes, run release
      benchmarks or relevant targeted benchmarks.
