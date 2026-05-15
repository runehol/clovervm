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

1. **Fast iterator protocol**

   Finish the iterator protocol in a way that preserves the current loop
   performance direction. Important pieces include a reusable `range` object,
   fresh iterators from `iter(range_obj)`, fast paths for range/list/tuple
   iteration, and a clean generic `iter()` / `next()` fallback. Iterator
   exhaustion should continue to integrate with the managed exception-table
   machinery.

2. **Specific VM exceptions**

   Replace generic runtime failures with specific VM exceptions for overflow,
   type errors, unsupported operations, descriptor failures, and other slow
   paths. This is a prerequisite for making later object-model and language
   features behave predictably without hiding failures in C++ helpers.

3. **Interpreter-controlled descriptor execution**

   Lookup already classifies descriptor work into plans. The next step is to
   execute `__get__`, `__set__`, and `__delete__` through explicit interpreter
   or VM-controlled dispatch so Python-visible execution, allocation, and
   exceptions are not hidden inside lookup/classification helpers.

4. **Keyword calls and richer call adaptation**

   Add keyword call support for ordinary functions and constructors, then
   extend toward `**kwargs` and richer call forms. This should extend the
   existing call-plan model rather than turning `CallSimple` into one generic
   slow path.

5. **Constructor semantics beyond tier-1 thunks**

   Expand constructor behavior past the current ordinary-class path. Remaining
   work includes keyword constructor calls, custom `__new__`, custom metaclass
   `__call__`, and normalization of constructor failures into specific VM
   exceptions.

6. **Full Python dict hashing and equality**

   Python `dict` needs arbitrary-key hashing and equality semantics rather than
   the current string-key-oriented internal assumptions. This matters for real
   Python code, imports, module namespaces, mappings, and future library work.

7. **Module objects and import system**

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

8. **Attribute hooks and escaped bound methods**

    Implement `__getattribute__`, `__getattr__`, `__setattr__`, and
    `__delattr__`, and add observable bound-method objects for escaped method
    values such as `f = obj.m`. Direct method-call fast paths should remain
    allocation-free when the bound method does not escape.

9. **Slices**

    Add parse, lowering, and runtime support for `a[i:j:k]`, including
    list/tuple/string slicing and slice assignment/deletion where appropriate.
    This is useful and contained, but less foundational than memory, iteration,
    call, descriptor, and module work.

10. **Range object completeness**

    If not completed as part of the fast iterator protocol, finish `range` as a
    proper reusable object with length, indexing, containment, representation,
    and iteration semantics.

11. **Memory substrate policy and placement**

    Deferred reference counting now has a correct single-threaded baseline:
    safepoint root filtering, ZCT processing, bitmap-discovered young objects,
    descriptor-driven teardown, and slab release are in place. Remaining memory
    work is important but no longer the main roadmap blocker: production
    reclamation triggers, useful counters, threshold tuning, and size-partitioned
    thread-local heaps should proceed when memory growth, fragmentation, or
    benchmark evidence makes them urgent. The design should remain compatible
    with later explicit native-transition roots, multi-threaded safepoint
    coordination, and no-GIL atomic refcount/lifecycle state.

12. **Generators, `yield`, and `yield from`**

    Generators create long-lived suspended frames, so they should wait until the
    memory/root model is reliable. `yield from` also needs careful interaction
    with `StopIteration.value` and internal no-value sentinels.

13. **Comprehensions and richer syntax**

    Add list/dict/set comprehensions, generator expressions, more assignment
    targets, richer string syntax, and other surface-area features after the
    runtime substrate and major semantic protocols are in better shape.

## Near-Term Track

The recommended next major track is language/runtime behavior, starting with the
fast iterator protocol. It is high impact, mostly interpreter-local, and does not
require beginning JIT implementation.

Near-term order:

1. Finish `range` object reuse and fresh `iter(range_obj)` behavior.
2. Keep range/list/tuple fast iteration paths aligned with the generic
   `iter()` / `next()` fallback.
3. Replace generic runtime failures with specific VM exceptions where current
   slow paths still blur Python-visible behavior.
4. Continue moving descriptor execution into explicit interpreter/VM-controlled
   paths rather than hidden lookup helpers.
5. Add keyword calls for ordinary functions and constructors.

Memory substrate follow-ups are still tracked in
[Memory Substrate Implementation Plan](memory-substrate-implementation-plan.md),
but they should not outrank the language/runtime work unless measurements show
memory growth, reclamation policy, or slab fragmentation has become the limiting
problem.

## Revisit Triggers

Revisit this ordering when:

- range/list/tuple/generic iterator paths are semantically complete enough for
  normal loops;
- keyword calls land for ordinary functions and constructors;
- module/import work becomes necessary to run broader Python source;
- module/global scope behavior starts duplicating enough shape/cache machinery
  that moving global lookups onto shape-backed ICs would simplify the runtime;
- memory measurements show that reclamation policy or ordinary slab mixing has
  become a practical limiter; or
- performance measurements show that a lower-priority item has become a
  bottleneck for existing benchmarks.
