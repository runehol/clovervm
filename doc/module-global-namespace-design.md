# Module Global Namespace Design

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Module globals storage, lookup, builtins resolution, mapping views, and cache validity |
| Owning layers | Runtime modules, scope metadata, interpreter globals access, and inline caches |
| Validated against | `112cfe9` (2026-05-22) |
| Supersedes | N/A |

This note describes the implemented split between lexical scope metadata,
module objects, and runtime global lookup.

For remaining follow-up work, see
[Module Global Namespace Implementation Plan](module-global-namespace-implementation.md).

The short version is:

- `Scope` is compiler/name-binding metadata, not runtime module storage.
- Module globals live on `ModuleObject`.
- `ModuleObject` storage is shape-backed object storage.
- Module attribute access and module global storage share the same backing
  storage.
- `LdaGlobal` is still a distinct global-namespace operation, not attribute
  lookup.

## Responsibilities

Use separate names for the separate jobs:

- **Scope**
  Compiler-side name classification and local/class slot metadata. It tracks
  parent scope identity for future nonlocal analysis, name-to-slot lookup,
  slot-name metadata, and insertion-order entries. It does not own runtime value
  cells and does not encode parent fallback inside `not_present`.

- **Frame locals**
  Runtime storage for function locals and current class-body execution. Named
  slots are initialized to `not_present` in the frame.

- **Module object**
  Runtime object that owns module identity and the module global namespace. Its
  shape-backed own-property storage is the normal backing store for top-level
  assignments, global lookup's first probe, and module attributes.

- **Builtins module**
  The VM-owned module object used as the default builtins namespace. It is a
  module object, but it is constructed without a visible `__builtins__` binding.

## Module Object Layout

`ModuleObject` is a heap object with ordinary object identity and shape-backed
attribute storage. Its class installs predefined descriptors for:

```text
slot 0: __name__
slot 1: __builtins__
```

The module layout reserves 256 inline storage slots. Ordinary module globals
start after the predefined slots.

The constructor requires an explicit builtins binding. That makes the two module
creation modes visible at the call site:

```cpp
// Ordinary module.
thread->make_module_object(name, vm->global_builtins_module().raw_value());

// The builtins module itself.
make_immortal_object_raw<ModuleObject>(builtins_name, Value::not_present());
```

Ordinary modules therefore expose a real `__builtins__` attribute. The builtins
module does not expose a self-referential `__builtins__` binding unless one is
explicitly added later.

`__builtins__` is a readonly module attribute through ordinary attribute and
global store/delete paths. Runtime code can still update it through
`set_builtins_binding` and `delete_builtins_binding`, which also invalidate
module lookup state.

## Global Lookup

Global bytecode uses name constants:

```text
LdaGlobal name, read_cache
StaGlobal name, mutation_cache
DelGlobal name
```

`LdaGlobal` resolves against the code object's defining module first. If the
name is not present there, it resolves against the module's effective builtins
namespace:

```text
defining_module[name]
effective_builtins[name]
```

This is not module attribute lookup. It does not consult the module class,
descriptor protocol, `__getattribute__`, or module-level `__getattr__`.

`StaGlobal` writes only to the defining module. `DelGlobal` deletes only from
the defining module. Missing reads and missing deletes raise `NameError`.

The defining module is carried by `CodeObject`; functions and class-body code
inherit it from the code object that creates them. A function global lookup uses
the function's defining module, not the caller's module.

## Builtins Resolution

`ModuleObject::get_module_builtins_lookup()` resolves the effective builtins
object from the module's `__builtins__` binding. If the binding is not present,
it falls back to the VM default builtins module.

The current cacheable path supports module-valued builtins. Non-module builtins
objects are represented as explicit uncacheable lookup results; dict-like
builtins traversal remains future work.

Because ordinary modules are created with an explicit `__builtins__` binding,
user code can access builtins through ordinary module attribute syntax:

```python
__builtins__.range
__builtins__.x = 1
del __builtins__.x
```

Those operations use normal module object attribute semantics over the builtins
module's shape-backed storage.

## Attribute Lookup

Module attribute access is ordinary object attribute access over module storage:

```text
module.x
module.x = value
del module.x
```

These operations route through the attribute descriptor, plan, and cache
machinery. They share the same shape-backed storage as top-level module globals,
but they do not have builtin fallback.

This boundary is intentional:

```text
module.x:
  object attribute lookup on the module object
  no global/builtin namespace fallback

LdaGlobal x:
  defining module namespace lookup
  effective builtins namespace lookup
  no module class lookup
```

## Inline Caches And Validity

Module-global load and store opcodes use side-array inline caches on the code
object:

- `module_global_read_caches`
- `module_global_mutation_caches`

The inline-cache entry is conceptually separate from the full lookup plan, but
its payload is the cacheable plan. Cacheable load/store plans carry one
validity cell:

```text
cache valid iff lookup_validity_cell != nullptr && lookup_validity_cell->is_valid()
```

Load caches store slot plans. Store caches store existing-slot plans. Delete is
not cached.

Validity comes from the lookup result:

- module globals validity protects module hit assumptions
- module builtins validity protects resolved-builtins assumptions
- modules used as builtins keep attached validity cells from consumers
- module shape changes and builtins-binding changes invalidate the relevant
  cells

Existing-slot writes do not invalidate module lookup state. Add/delete shape
transitions do invalidate lookup state.

## Scope After The Migration

`Scope` no longer contains module runtime values. It is still used for:

- local slot metadata
- class-body frame slot metadata and class namespace harvesting
- parent scope identity for future nonlocal analysis
- ordered name metadata where later dictionary-like presentation may need it

It no longer contains:

- module/global values
- parent-slot fallback payloads
- deleted-name slot-index rematerialization
- indexed `Value::not_present(parent_slot_idx)` encoding

When class bodies execute today, they use frame slots for runtime storage and
the existing `BuildClass` slot-harvesting path. A more Python-compatible class
namespace path is a follow-up.

## Remaining Design Boundaries

The following are intentionally still separate follow-ups:

- `globals()` and module-scope `locals()` as live mutable views over module
  storage.
- `module.__dict__` as a mapping view over module storage.
- dict-like `__builtins__` traversal and cache invalidation.
- import and multi-module execution tests.
- class-body `LOAD_NAME` / `STORE_NAME` / `DEL_NAME` and active class namespace
  execution.
- large-shape lookup acceleration for cold module-global lookups.

## Non-Goals

This design does not:

- put shapes on function locals
- make global lookup invoke module attribute lookup
- expose builtin fallback as module own attributes
- make functions search caller modules
- make class-body locals ordinary closure parents for methods
- force Python `dict`, symbol scopes, and object shapes into one concrete table
  type
