# Development Priorities

This document records the current priority order for major CloverVM
development. It is not a short-lived task checklist. The goal is to make the
next few large moves explicit, including the dependencies that should constrain
JIT, language, and runtime work.

## Hard Constraints

- Real JIT work must preserve the current memory substrate assumptions: root
  discovery, safepoint visibility, heap scanning, descriptor-driven teardown,
  and deallocation all have to remain explicit in the design.
- Keep interpreter hot paths measurable. Opcode handlers listed as hot path
  should remain compatible with the existing frameless/musttail dispatch
  constraints.
- Do not turn feature work into hidden semantic shortcuts. If a feature can run
  Python bytecode, allocate observably, invoke descriptors, or raise, it should
  be routed through explicit VM dispatch and pending-exception propagation.
- Keep fallibility visible in types. Native/interpreter boundaries may still use
  `[[nodiscard]] Value` plus `Value::exception_marker()`, but typed internal
  APIs should move toward `Expected<T>`, including non-`Value` results such as
  parser indexes.
- Prefer architecture that supports future no-GIL and C-extension work without
  forcing immediate implementation of those larger goals.

## Priority Order

1. **Iterator protocol completion and range object**

   The first iterator slice is no longer greenfield: generic `iter()` /
   `next()`, generic `for` lowering, compact `StopIteration` transport, and
   range/list/tuple iterator objects exist. The remaining foundational gap is
   that public `range()` still returns a mutable iterator directly. Introduce a
   reusable `range` object, make `iter(range_obj)` produce fresh
   `RangeIterator`s, and keep the direct range/list/tuple fast paths aligned
   with the generic iterator protocol. Iterator exhaustion should continue to
   integrate with the managed exception-table machinery.

2. **Specific VM exceptions and typed fallibility**

   Replace generic runtime failures with specific VM exceptions for overflow,
   type errors, unsupported operations, descriptor failures, and other slow
   paths. This is a prerequisite for making later object-model and language
   features behave predictably without hiding failures in C++ helpers. Continue
   converting fallible internal helpers to `Expected<T>` where that makes the
   success type precise, including non-handle results such as `Expected<int32_t>`
   in parser or compiler code.

3. **Interpreter-controlled descriptor execution**

   Lookup already classifies descriptor work into plans. The next step is to
   execute `__get__`, `__set__`, and `__delete__` through explicit interpreter
   or VM-controlled dispatch so Python-visible execution, allocation, and
   exceptions are not hidden inside lookup/classification helpers.

4. **Guarded binary-operation plans and type profiling**

   Preserve direct SMI-plus-SMI arithmetic as the primary hot path. The common
   integer case should remain a direct tag check and checked arithmetic path,
   without paying for uniform shape lookup or generic cache probing.

   For cases that fall out of that hot path, move binary operators such as `+`
   from ad hoc special cases plus generic failure into explicit guarded plans.
   The resolver should own Python operator semantics, including type special
   cases, special-method lookup on the type, reflected methods,
   `NotImplemented`, and eventual `TypeError` formation. Executing a Python
   `__add__` or `__radd__` should reuse the call-plan machinery as a
   special-method call, not perform ordinary instance attribute lookup.

   The fallback inline cache should be polymorphic and keyed by operand shape
   pairs, using heap-object shapes and the VM's inline-value shapes as the same
   profiling vocabulary. Each cache entry gives both the interpreter and the
   future JIT a type profile: guarded shape-pair fast paths, exact
   special-method call targets, validity cells, and a megamorphic fallback when
   the site stops being predictable.

5. **Keyword calls and richer call adaptation**

   Add keyword call support for ordinary functions and constructors, then
   extend toward `**kwargs` and richer call forms. This should extend the
   existing call-plan model rather than turning `CallSimple` into one generic
   slow path.

6. **Constructor semantics beyond tier-1 thunks**

   Expand constructor behavior past the current ordinary-class path. Remaining
   work includes keyword constructor calls, custom `__new__`, custom metaclass
   `__call__`, and normalization of constructor failures into specific VM
   exceptions.

7. **Full Python dict hashing and equality**

   Python `dict` needs arbitrary-key hashing and equality semantics rather than
   the current string-key-oriented internal assumptions. This matters for real
   Python code, imports, module namespaces, mappings, and future library work.

8. **Import system completion**

   The import foundation has landed: module globals are shape-backed
   `ModuleObject` storage, `globals()` and module-scope `locals()` expose live
   `SlotDict` views, `sys.modules` and `sys.path` exist, `__main__` is a real
   module in `sys.modules`, source modules and regular packages load through
   the bootstrap source finder, `sys` and `builtins` can be rediscovered by an
   internal builtin finder, simple namespace package directories can be used as
   package parents, and import statements call the mutable public
   `builtins.__import__` hook. Absolute imports, dotted imports, packages,
   aliases, comma import lists, parenthesized from-import lists, submodule
   parent binding, explicit relative from-imports, and module-scope star imports
   are implemented.

   The remaining import work is no longer "invent the module system"; it is
   completion and compatibility. The near-term gap is a small Python-visible
   spec/loader surface instead of exposing `__spec__` and `__loader__` as
   `None`.

   The larger follow-ups remain public `sys.meta_path`, path hooks/importer
   cache, multi-portion namespace package merging, bytecode caches, frozen
   modules, extension modules, importlib surface area, and exact module
   namespace compatibility such as stable `module.__dict__` identity.

9. **Attribute hooks and escaped bound methods**

    Implement `__getattribute__`, `__getattr__`, `__setattr__`, and
    `__delattr__`, and add observable bound-method objects for escaped method
    values such as `f = obj.m`. Direct method-call fast paths should remain
    allocation-free when the bound method does not escape.

10. **Slices**

    Add parse, lowering, and runtime support for `a[i:j:k]`, including
    list/tuple/string slicing and slice assignment/deletion where appropriate.
    This is useful and contained, but less foundational than iteration, call,
    descriptor, and module work.

11. **Range object completeness**

    After public `range()` stops returning a mutable iterator directly, finish
    the remaining range surface: length, indexing, containment, representation,
    equality where appropriate, and edge-case arithmetic semantics.

12. **Generators, `yield`, and `yield from`**

    Generators create long-lived suspended frames, so they should wait until the
    memory/root model is reliable. `yield from` also needs careful interaction
    with `StopIteration.value` and internal no-value sentinels.

13. **Comprehensions and richer syntax**

    Add list/dict/set comprehensions, generator expressions, more assignment
    targets, richer string syntax, and other surface-area features after the
    runtime substrate and major semantic protocols are in better shape.

## Near-Term Track

The recommended immediate track is to finish the current import bootstrap slice
before switching contexts. Imports now work well enough that the remaining
near-term pieces are smaller and more sharply defined than they were when this
document last listed module work as a future design option.

Near-term order:

1. Replace exposed `module.__spec__ = None` and `module.__loader__ = None` with
   a small Python-visible spec/loader surface.
2. Revisit `sys.meta_path` only after internal finder/loader objects have a
   stable shape worth exposing.
3. Return to the reusable public `range` object and fresh `iter(range_obj)`
   behavior.
4. Continue replacing generic runtime failures with specific VM exceptions and
   typed `Expected<T>` results where useful.
5. Add guarded binary-operation plans and polymorphic inline caches so operator
   sites collect useful type profiles for the future JIT.
6. Continue moving descriptor execution into explicit interpreter/VM-controlled
   paths rather than hidden lookup helpers.
7. Add keyword calls for ordinary functions and constructors.

## Revisit Triggers

Revisit this ordering when:

- source modules expose a useful Python-visible `__spec__` and `__loader__`;
- `range()` is a reusable object and range/list/tuple/generic iterator paths are
  semantically complete enough for normal loops;
- descriptor `__get__`, `__set__`, and `__delete__` execution no longer hides
  Python-visible behavior inside lookup helpers;
- binary operators use guarded plans/PICs for type-specialized fast paths,
  special-method calls, reflected methods, and `NotImplemented` fallback;
- keyword calls land for ordinary functions and constructors;
- module namespace compatibility, especially `module.__dict__`, becomes
  necessary for broader Python source;
- performance measurements show that a lower-priority item has become a
  bottleneck for existing benchmarks.
