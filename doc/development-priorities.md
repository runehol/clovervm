# Development Priorities

This document records the current priority order for major CloverVM
development. It is not a short-lived task checklist. The goal is to make the
next few large moves explicit, including the dependencies that should constrain
JIT, language, and runtime work.

## Hard Constraints

- Do not start real JIT implementation before the memory substrate has a
  working baseline for root discovery, heap scanning, and deallocation.
- Keep interpreter hot paths measurable. Opcode handlers listed as hot path
  should remain compatible with the existing frameless/musttail dispatch
  constraints.
- Do not turn feature work into hidden semantic shortcuts. If a feature can run
  Python bytecode, allocate observably, invoke descriptors, or raise, it should
  be routed through explicit VM dispatch and pending-exception propagation.
- Prefer architecture that supports future no-GIL and C-extension work without
  forcing immediate implementation of those larger goals.

## Priority Order

1. **Memory substrate: root scanning, heap scanning, and deallocation**

   Deferred reference counting depends on a reliable way to discover live
   managed roots, validate zero-count candidates, scan heap references, and tear
   down objects. This should reach at least a correct single-threaded baseline
   before JIT work begins. The design should remain compatible with later
   safepoint coordination, explicit native-transition roots, and multi-threaded
   execution.

2. **Layout-ID-driven scanning and deallocation descriptors**

   Treat this as the implementation path for the memory substrate, not as a
   broad object-layout rewrite for its own sake. Introduce a `NativeLayoutId`
   keyed descriptor table, validate it against current layout metadata, migrate
   fixed layouts first, then dynamic layouts, and keep common static paths
   data-driven and fast.

3. **Fast iterator protocol**

   Finish the iterator protocol in a way that preserves the current loop
   performance direction. Important pieces include a reusable `range` object,
   fresh iterators from `iter(range_obj)`, fast paths for range/list/tuple
   iteration, and a clean generic `iter()` / `next()` fallback. Iterator
   exhaustion should continue to integrate with the managed exception-table
   machinery.

4. **Specific VM exceptions**

   Replace generic runtime failures with specific VM exceptions for overflow,
   type errors, unsupported operations, descriptor failures, and other slow
   paths. This is a prerequisite for making later object-model and language
   features behave predictably without hiding failures in C++ helpers.

5. **Interpreter-controlled descriptor execution**

   Lookup already classifies descriptor work into plans. The next step is to
   execute `__get__`, `__set__`, and `__delete__` through explicit interpreter
   or VM-controlled dispatch so Python-visible execution, allocation, and
   exceptions are not hidden inside lookup/classification helpers.

6. **Keyword calls and richer call adaptation**

   Add keyword call support for ordinary functions and constructors, then
   extend toward `**kwargs` and richer call forms. This should extend the
   existing call-plan model rather than turning `CallSimple` into one generic
   slow path.

7. **Constructor semantics beyond tier-1 thunks**

   Expand constructor behavior past the current ordinary-class path. Remaining
   work includes keyword constructor calls, custom `__new__`, custom metaclass
   `__call__`, and normalization of constructor failures into specific VM
   exceptions.

8. **Full Python dict hashing and equality**

   Python `dict` needs arbitrary-key hashing and equality semantics rather than
   the current string-key-oriented internal assumptions. This matters for real
   Python code, imports, module namespaces, mappings, and future library work.

9. **Module objects and import system**

   Imports are a major usability milestone, but should be built on a coherent
   module/global namespace model. One attractive direction is shape-backed
   module objects so bytecode global access, `module.x`, `globals()`, and
   eventual module `__dict__` views agree on one semantic store while preserving
   builtin fallback rules.

   A broader version of this would move modules and scopes further onto the
   shape/inline-cache/validity-cell path, using cached global lookup results
   rather than the current slot shortcut machinery. That could simplify the
   runtime model and make global lookups resemble the rest of the object-cache
   system. The stronger motivation is lookup cost and JIT shape: a cached
   builtin or module-global lookup can be guarded by validity cells instead of
   repeatedly walking module/builtin scope state. That gives future JIT code a
   compact guard-and-load model for globals rather than baking in scope-chain
   traversal.

   Treat this as a design option, not a prerequisite: the current scope slot
   system works well enough that this should be driven by import/module
   semantics, cache invalidation needs, JIT/root-map needs, or measured
   complexity rather than by aesthetic unification alone.

10. **Attribute hooks and escaped bound methods**

    Implement `__getattribute__`, `__getattr__`, `__setattr__`, and
    `__delattr__`, and add observable bound-method objects for escaped method
    values such as `f = obj.m`. Direct method-call fast paths should remain
    allocation-free when the bound method does not escape.

11. **Slices**

    Add parse, lowering, and runtime support for `a[i:j:k]`, including
    list/tuple/string slicing and slice assignment/deletion where appropriate.
    This is useful and contained, but less foundational than memory, iteration,
    call, descriptor, and module work.

12. **Range object completeness**

    If not completed as part of the fast iterator protocol, finish `range` as a
    proper reusable object with length, indexing, containment, representation,
    and iteration semantics.

13. **Generators, `yield`, and `yield from`**

    Generators create long-lived suspended frames, so they should wait until the
    memory/root model is reliable. `yield from` also needs careful interaction
    with `StopIteration.value` and internal no-value sentinels.

14. **`with` statement and context managers**

    Context managers are attractive once exception propagation and descriptor
    execution are less provisional. The existing `try` / `finally` machinery
    should make the eventual lowering tractable.

15. **Comprehensions and richer syntax**

    Add list/dict/set comprehensions, generator expressions, more assignment
    targets, richer string syntax, and other surface-area features after the
    runtime substrate and major semantic protocols are in better shape.

## Near-Term Track

The recommended next major track is the memory substrate:

1. Establish root scanning over managed frames and any explicit native
   transition roots needed by current runtime helpers.
2. Add heap object scanning and teardown using current metadata.
3. Introduce layout descriptors and validate them against the current metadata
   path.
4. Route fixed native layouts through descriptors.
5. Add dynamic layout handlers for variable-size builtins.
6. Add a minimal zero-count/reclamation policy that favors correctness over
   slab-reuse sophistication.

Fast iterator protocol work is the best parallel or immediately-following
language/runtime track because it is high impact, mostly interpreter-local, and
does not require beginning JIT implementation.

## Revisit Triggers

Revisit this ordering when:

- root scanning, heap scanning, and deallocation have a working baseline;
- layout descriptors cover fixed and dynamic native layouts;
- range/list/tuple/generic iterator paths are semantically complete enough for
  normal loops;
- keyword calls land for ordinary functions and constructors;
- module/import work becomes necessary to run broader Python source;
- module/global scope behavior starts duplicating enough shape/cache machinery
  that moving global lookups onto shape-backed ICs would simplify the runtime;
  or
- performance measurements show that a lower-priority item has become a
  bottleneck for existing benchmarks.
