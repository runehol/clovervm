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
- Keep fallibility visible in types. `libclovervm` is built without C++
  exceptions, so VM failures must travel through pending Python exception state,
  `Value::exception_marker()`, or `Expected<T>` rather than C++ exception
  transport. Native/interpreter boundaries may still use `[[nodiscard]] Value`
  plus `Value::exception_marker()`, while typed internal APIs should continue to
  use `Expected<T>`, including non-`Value` results such as parser indexes.
- Prefer architecture that supports future no-GIL and C-extension work without
  forcing immediate implementation of those larger goals.

## Priority Order

1. **Full pystone benchmark**

   The current `pystone_lite.py` benchmark exercises a useful subset, but the
   full Python 3-era pystone benchmark is now close enough to be valuable. The
   benchmark harness already has external CPython timing through
   `cpython_runner.py`, and Google Benchmark externally times CloverVM
   `run(n)`, so full pystone should be adapted into the same harness rather
   than using pystone's internal timing.

   String ordering and rich-comparison dispatch are no longer the primary
   blocker. The next step is to adapt full pystone into the benchmark harness
   and use the result to choose the next runtime slice. Setup-only unsupported
   constructs such as list comprehensions, list multiplication, and unpacking
   assignment can be rewritten in the benchmark source without changing the
   measured loop body, but benchmark-body semantics should remain intact.

2. **Implicit protocol dispatch and cached special calls**

   Move guarded special-method dispatch to implicit protocols: cached dunder
   method calls for `__len__`, `__iter__`, `__next__`, numeric conversions, and
   other VM-invoked protocols.

   These should reuse the guarded special-method call model where possible, but
   not inherit operator-specific assumptions such as reflected fallback or
   `NotImplemented` continuation. Recent constructor and conversion benchmarks
   showed that once direct `str(int)` avoided generic lookup, pressure moved to
   cached calls, Python-level protocol lowering, and short-lived allocations.

3. **Richer call adaptation and generic callable protocol**

   Extend the call-plan model to the remaining Python call forms: runtime
   support for positional-only parameters, callee `**kwargs`, caller `*args`,
   caller `**kwargs`, richer duplicate/error ordering, arbitrary object
   `__call__`, and native-to-managed keyword-call APIs.

   Keep this as an extension of the existing positional and keyword call-plan
   model. Do not collapse `CallPositional`, `CallKeyword`, direct positional
   method calls, and direct keyword method calls into one broad generic slow
   path unless measurements show the current specialization is the wrong shape.

   Call adaptation must remain call-site-shaped, not just callee-shaped. A
   function with defaults can still use the fixed-arity frame-entry path when a
   positional call supplies every parameter slot; default initialization is only
   needed for call sites that leave defaulted slots unfilled.

4. **Interpreter-controlled descriptor execution**

   Lookup already classifies descriptor work into plans. The next step is to
   execute `__get__`, `__set__`, and `__delete__` through explicit interpreter
   or VM-controlled dispatch so Python-visible execution, allocation, and
   exceptions are not hidden inside lookup/classification helpers.

5. **Full Python dict hashing and equality**

   Python `dict` needs arbitrary-key hashing and equality semantics rather than
   the current string-key-oriented internal assumptions. This matters for real
   Python code, imports, module namespaces, mappings, and future library work.

6. **Short-lived allocation and reclamation pressure**

    Repeated constructor/conversion and get-slice benchmarks now spend
    significant time allocating and reclaiming tiny temporary objects, especially
    strings and short list/tuple slice results. Before treating formatting,
    protocol dispatch, or slice copy loops as the primary remaining bottleneck,
    measure whether the benchmark has become an allocator/reclamation workload.
    Candidate work includes evaluating a generational, non-moving,
    stop-the-world mark-and-sweep allocator, as well as improving
    zero-count-table processing, slab reuse, and size-class behavior for very
    small short-lived objects.

7. **Slice write/delete support when it becomes the shortest path**

   Keep `slice.__new__` and syntax construction validation-free: slice fields
   are raw objects, and `__index__` conversion must happen at consumption time
   because it can run Python-visible side effects.

   List slice `__setitem__` and `__delitem__`, plus any future equality/hash
   decisions, should wait until a concrete benchmark or stdlib bringup task
   needs them.

8. **Attribute hooks and escaped bound methods**

    Implement `__getattribute__`, `__getattr__`, `__setattr__`, and
    `__delattr__`, and add observable bound-method objects for escaped method
    values such as `f = obj.m`. Direct method-call fast paths should remain
    allocation-free when the bound method does not escape.

9. **Inner functions with variable capture**

    Implement nested functions that capture variables from enclosing function
    scopes, following the cell-based design in
    [Closure Cells](closure-cells.md). This should cover free-variable reads,
    captured local rebinding, shared cells across multiple closures, and
    `nonlocal` binding analysis rather than treating captures as copied values
    or parent-frame lookups.

    The implementation should keep ordinary uncaptured locals as direct frame
    slots and introduce cell storage only for bindings that Python semantics
    require to be shared with nested functions. Captured bindings are heap
    objects and therefore need explicit lifetime, root visibility, teardown, and
    pending-exception behavior for uninitialized cell reads.

10. **Remaining operator and expression surface**

    Remaining operator surface should be filled in only where it has a clear
    semantic design and benchmark or compatibility demand.

    Known remaining gaps include ternary `pow(a, b, modulo)`, `@` / matrix
    multiplication, `in` / `not in`, and the `operator` module helpers that
    mirror these operations. Ternary power is high-priority because it extends
    the dispatch substrate; containment remains lower until it has a design
    that does not misuse `NotImplemented` continuation semantics.

11. **Generators, `yield`, and `yield from`**

    Generators create long-lived suspended frames, so they should wait until the
    memory/root model is reliable. `yield from` also needs careful interaction
    with `StopIteration.value` and internal no-value sentinels.

12. **Comprehensions and richer syntax**

    Add list/dict/set comprehensions, generator expressions, more assignment
    targets, richer string syntax, and other surface-area features after the
    runtime substrate and major semantic protocols are in better shape.

## Revisit Triggers

Revisit this ordering when:

- descriptor `__get__`, `__set__`, and `__delete__` execution no longer hides
  Python-visible behavior inside lookup helpers;
- full pystone runs in the benchmark harness and shows a different blocker than
  benchmark-source adaptation;
- implicit protocols such as `__len__`, `__iter__`, `__next__`, and numeric
  conversion have guarded dispatch plans comparable to operator dispatch;
- callee `**kwargs`, caller `*args` / `**kwargs`, positional-only parameters, or
  generic `__call__` become the next blocker for stdlib module bringup;
- importlib, public finder/loader APIs, path hooks, or exact module namespace
  compatibility become necessary for broader Python source;
- public `range()` semantics become a practical blocker rather than a visible
  cleanup item;
- performance measurements show that a lower-priority item has become a
  bottleneck for existing benchmarks.
