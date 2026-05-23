# Module Global Namespace Implementation Plan

This file tracks follow-up work after the module-global namespace migration.
The implemented design is described in
[Module Global Namespace Design](module-global-namespace-design.md).

## Current State

The migration from scope-backed module globals to module-object globals is
landed.

Implemented behavior:

- `ModuleObject` owns module identity and module global storage.
- Module global storage is shape-backed object storage.
- `__name__` and `__builtins__` are predefined module slots.
- Ordinary modules are constructed with an explicit `__builtins__` binding to
  the VM global builtins module.
- The builtins module itself is constructed with `Value::not_present()` for its
  builtins binding.
- `CodeObject` carries a defining-module reference.
- `LdaGlobal`, `StaGlobal`, and `DelGlobal` operate by constant-table name over
  the defining module.
- `LdaGlobal` uses defining-module lookup followed by effective builtins lookup.
- `StaGlobal` stores only into the defining module.
- `DelGlobal` deletes only from the defining module.
- Module-global load/store inline caches guard cacheable slot plans with one
  plan-owned validity cell.
- Delete is intentionally uncached.
- Module attribute read/write/delete uses normal object attribute semantics over
  the same shape-backed module storage.
- Trusted builtins initialization writes directly into the global builtins
  module.
- `globals()` is implemented as a trusted builtin that lowers
  `__clover_globals__()` to an interpreter intrinsic.
- The `Globals` intrinsic returns a fresh live `SlotDict` view over the caller
  code object's defining module storage.
- Module-scope `locals()` is implemented as a trusted builtin that lowers
  `__clover_locals__()` to an interpreter intrinsic.
- The `Locals` intrinsic returns a fresh live `SlotDict` view for module code
  and raises `UnimplementedError` for code objects with non-null local scope.
- `Scope` no longer owns runtime value cells and no longer encodes parent-scope
  fallback in `not_present`.
- The legacy scope bridge, legacy global bytecodes, and transitional
  compatibility paths have been removed.

Known performance note:

- Global add/delete is slower than the old scope-slot path because it now
  performs real shape transitions and invalidates lookup state. That is accepted
  for now; repeated add/delete is not expected to be a hot workload.

## Remaining Work

### Module Mapping Views

- [x] Implement `globals()` as a live mutable view over module object storage.
- [x] Implement module-scope `locals()` as the same live mutable view.
- [x] Expose `module.__dict__` as a live `SlotDict` view over module object
      storage.
- [x] Add tests that top-level stores and `globals()["x"]` observe the same own
      module binding where supported.
- [x] Add tests that builtin fallback does not appear as a module own attribute
      or module mapping entry.

### Import And Multi-Module Execution

- [x] Bootstrap an immortal `sys` module.
- [x] Bootstrap `sys.modules` as the VM-owned imported-modules dict with
      `"builtins"` and `"sys"` entries.
- [x] Bootstrap `sys.path` as a list initialized to
      `["", CL_BUILD_STDLIB_DIR, CL_STDLIB_DIR]`.
- [x] Add a source-tree `stdlib/` directory for ordinary importable system
      modules.
- [x] Add tests that functions use their defining module, not the caller's
      module, for global lookup.
- [x] Add tests that module-object `__builtins__` reassignment and deletion
      affect builtin fallback and cache invalidation correctly.
- [ ] Add dict-like `__builtins__` traversal after dict namespace semantics and
      cache invalidation have a dedicated design.

### Class Namespace Follow-Up

- [ ] Add `active_locals_namespace` to frame state.
- [ ] Add `LoadName`.
- [ ] Add `StoreName`.
- [ ] Add `DelName`.
- [ ] Compile class body name access to name opcodes.
- [ ] Create a temporary class namespace before class body execution.
- [ ] Make `BuildClass` consume the active class namespace.
- [ ] Make class-body `locals()` return the active class namespace.
- [ ] Preserve the current class local-slot harvest path until this stage is
      complete.
- [ ] Add tests that class body locals are visible in class construction.
- [ ] Add tests that nested methods do not capture ordinary class-body names as
      closure locals.

### Large Namespace Lookup

- [ ] Profile cold lookup costs for large module shapes.
- [ ] Decide whether shape descriptor lookup needs a side index.
- [ ] If added, keep descriptor order authoritative and make the index an
      acceleration structure only.

### Verification And Cleanup

- [ ] Update other architecture docs that still describe module globals as
      `Scope` storage.
- [ ] For performance-sensitive cache or interpreter changes, run release
      benchmarks or targeted benchmarks.
