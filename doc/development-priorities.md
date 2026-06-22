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
- Every VM-implicit Python-visible execution path should be explicit and
  inspectable before serious JIT code generation begins. Descriptor execution,
  callable-object dispatch, iteration protocol calls, implicit conversion
  protocols, and attribute hooks need dispatch plans and inline-cache shapes
  that future compiled code can guard, inline, or exit from.
- Keep fallibility visible in types. `libclovervm` is built without C++
  exceptions, so VM failures must travel through pending Python exception state,
  `Value::exception_marker()`, or `Expected<T>` rather than C++ exception
  transport. Native/interpreter boundaries may still use `[[nodiscard]] Value`
  plus `Value::exception_marker()`, while typed internal APIs should continue to
  use `Expected<T>`, including non-`Value` results such as parser indexes.
- Prefer architecture that supports future no-GIL and C-extension work without
  forcing immediate implementation of those larger goals.

## Priority Order

1. **Interpreter-controlled descriptor execution**

   Lookup already classifies descriptor work into plans. The next step is to
   execute `__get__`, `__set__`, and `__delete__` through explicit interpreter
   or VM-controlled dispatch so Python-visible execution, allocation, and
   exceptions are not hidden inside lookup/classification helpers.

   Descriptor plans must carry the guards and binding decisions needed by a
   future JIT. A compiled attribute read, write, delete, or method call should
   be able to replay the same descriptor decision from cache metadata, or exit
   to the interpreter when the guard no longer proves that decision.

2. **Generic callable protocol and call-site IC contracts**

   Extend the call-plan model from ordinary functions, constructor thunks, and
   direct method calls to all callable-object shapes the VM can encounter:
   escaped bound methods, descriptor-produced callables, and arbitrary objects
   with `__call__`.

   Keep call adaptation call-site-shaped, not just callee-shaped. A warmed call
   site should expose enough normalized plan metadata for exact JIT-to-JIT calls
   later: callable guard, receiver binding, defaults copied, constructor thunk,
   target code object, pending-exception behavior, and any validity cells.
   Unsupported public call forms should remain explicit adapter edges, not
   implicit fallback machinery hidden inside the hot path.

   This does not require completing every Python call syntax first. Runtime
   support for positional-only parameters, callee `**kwargs`, caller `*args`,
   caller `**kwargs`, richer duplicate/error ordering, and native keyword-call
   convenience APIs can land later as additional call-plan variants.

3. **Implicit protocol dispatch and cached special calls**

   Move guarded special-method dispatch to implicit protocols: cached dunder
   method calls for `__len__`, `__iter__`, `__next__`, `__index__`, truthiness,
   numeric conversions, and other VM-invoked protocols.

   These should reuse the guarded special-method call model where possible, but
   not inherit operator-specific assumptions such as reflected fallback or
   `NotImplemented` continuation. Protocol caches should record the lookup,
   binding, call target, and continuation behavior that a JIT would otherwise
   have to rediscover.

4. **Traceable iteration protocol and loop plans**

   Iteration should be lowered so every implicit call and control-flow edge is
   visible: `__iter__`, `__next__`, public `StopIteration` consumption, and any
   direct internal iterator plan. Loop fast paths should produce feedback that a
   future JIT can inspect to decide whether it may skip protocol calls, inline a
   concrete iterator step, or leave an explicit cached `__next__` call in place.

   Direct builtin iteration plans are useful, but they are not a substitute for
   traceable public protocol calls. Exact immutable builtin plans may skip
   Python-visible calls only when their guards prove the same semantic result.

5. **Memory strategy and JIT root metadata**

   Decide whether first JIT work targets the current deferred-refcounting
   substrate or a generational non-moving mark-sweep collector. The decision
   must settle the contracts for write barriers or retain/release operations,
   safepoint records, accumulator publication, deopt metadata, transition
   stubs, native-stack switching, descriptor-driven teardown, and native object
   pinning.

   Implementing a new collector is not necessarily a prerequisite for the first
   compiler, but starting JIT code generation before the root and heap-write
   contracts are explicit risks rework in every compiled store, call, exit, and
   runtime transition.

6. **Split string-key dictionaries from general Python dictionaries**

   Preserve a string-key dictionary shape for namespaces and VM-internal maps
   where exact `str` keys give native hashing and equality with no
   Python-visible calls. This representation fits module globals, class
   namespaces, keyword-name tables, and other maps whose key contract is
   deliberately narrower than public Python `dict`.

   Add a separate general Python dictionary path for arbitrary keys. General
   dict lookup, insertion, and deletion must route `__hash__` and equality
   through explicit protocol dispatch with inspectable inline-cache metadata,
   because those operations can run Python bytecode, raise, mutate, or re-enter
   the VM. A future JIT should be able to distinguish native string-key lookup
   from general dict lookup with guarded hash/equality calls, not rediscover
   those hidden call sites inside dict probing helpers.

7. **Attribute hooks and escaped bound methods**

    Implement `__getattribute__`, `__getattr__`, `__setattr__`, and
    `__delattr__`, and add observable bound-method objects for escaped method
    values such as `f = obj.m`. Direct method-call fast paths should remain
    allocation-free when the bound method does not escape.

8. **Inner functions with variable capture**

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

9. **Remaining operator and expression surface**

    Remaining operator surface should be filled in only where it has a clear
    semantic design and benchmark or compatibility demand.

    Known remaining gaps include `@` / matrix multiplication and the `operator`
    module helpers that mirror existing operations.

10. **Class semantics cleanup**

    Class scopes are currently implemented as local scopes, but this is not
    correct. Instead these should be dynamic scopes backed by a dict that is
    eventually slurped up into the class object when the initialization is done.

11. **Generators, `yield`, and `yield from`**

    Generators create long-lived suspended frames, so they should wait until the
    memory/root model is reliable. `yield from` also needs careful interaction
    with `StopIteration.value` and internal no-value sentinels.

12. **Slice write/delete support when it becomes the shortest path**

    Keep `slice.__new__` and syntax construction validation-free: slice fields
    are raw objects, and `__index__` conversion must happen at consumption time
    because it can run Python-visible side effects.

    List slice `__setitem__` and `__delitem__`, plus any future equality/hash
    decisions, should wait until a concrete benchmark or stdlib bringup task
    needs them.

13. **Comprehensions and richer syntax**

    Add list/dict/set comprehensions, generator expressions, more assignment
    targets, richer string syntax, and other surface-area features after the
    runtime substrate and major semantic protocols are in better shape.

## Revisit Triggers

Revisit this ordering when:

- descriptor `__get__`, `__set__`, and `__delete__` execution is explicit,
  guarded, and replayable from cache metadata;
- ordinary functions, constructor thunks, direct methods, escaped bound
  methods, descriptor-produced callables, and arbitrary `__call__` objects all
  have explicit call-plan outcomes or explicit adapter edges;
- iteration lowering exposes `__iter__`, `__next__`, `StopIteration`
  consumption, and internal iterator plans as inspectable bytecode/cache
  metadata rather than hidden helper behavior;
- the memory strategy for first JIT work has an explicit contract for heap
  writes, safepoint publication, deopt/root maps, native-stack transitions, and
  object pinning;
- full pystone runs in the benchmark harness and shows a different blocker than
  benchmark-source adaptation;
- implicit protocols such as `__len__`, `__iter__`, `__next__`, and numeric
  conversion have guarded dispatch plans comparable to operator dispatch;
- callee `**kwargs`, caller `*args` / `**kwargs`, positional-only parameters, or
  exact CPython call diagnostics become the next blocker for stdlib module
  bringup;
- importlib, public finder/loader APIs, path hooks, or exact module namespace
  compatibility become necessary for broader Python source;
- public `range()` semantics become a practical blocker rather than a visible
  cleanup item;
- performance measurements show that a lower-priority item has become a
  bottleneck for existing benchmarks.
