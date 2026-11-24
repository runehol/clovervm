# Development Priorities

This document records the major language and runtime work that remains relevant
alongside JIT development. It is not the ordering authority for near-term
compiler slices; that order lives in
[JIT Near-Term Implementation Roadmap](jit-near-term-roadmap.md).

For bounded work that can land independently of this priority order, see
[Small Tasks](small-tasks.md).

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
  inspectable before compiled code depends on it. Descriptor execution,
  callable-object dispatch, iteration protocol calls, implicit conversion
  protocols, and attribute hooks need dispatch plans and inline-cache shapes
  that compiled code can guard, inline, or exit from as coverage expands.
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

   Interpreter attribute reads, writes, and deletes execute plain-function
   `__get__`, `__set__`, and `__delete__` hooks through explicit VM dispatch.
   Eligible lookups have guarded cache plans. Remaining work includes arbitrary
   callable descriptor hooks, class creation notifications through
   `__set_name__`, and descriptor-aware native method callbacks
   ([#149](https://github.com/runehol/clovervm/issues/149)). See
   [Descriptor Execution](descriptor-execution.md).

   Descriptor-aware direct calls still need a way to resolve the attribute and
   run its getter before evaluating arguments. The proposed `LoadMethod` would
   keep the selected callable and optional receiver live across argument
   evaluation and preserve allocation-free calls where binding permits.

   Descriptor plans must carry the guards and binding decisions needed by
   compiled code. A compiled attribute read, write, delete, or method call should
   be able to replay the same descriptor decision from cache metadata, or exit
   to the interpreter when the guard no longer proves that decision.

2. **Generic callable protocol and call-site IC contracts**

   Guarded call-target ICs cover ordinary functions, constructor thunks, and
   instances whose type MRO supplies a plain function for `__call__`. Direct
   method calls have their own lookup plans. Saved bound methods are callable
   via a managed forwarding function. See
   [Function Call Adaptation](function-call-adaptation.md).

   Extend call-target resolution to instances whose `__call__` needs
   descriptor binding or resolves to a non-function callable. Values returned
   by descriptor hooks can already be saved and called if they fit the
   supported call forms. Integrate that lookup with direct calls as described
   above.

   Keep call adaptation call-site-shaped, not just callee-shaped. A warmed call
   site should expose enough normalized plan metadata for exact JIT-to-JIT calls
   later: callable guard, receiver binding, defaults copied, constructor thunk,
   target code object, pending-exception behavior, and any validity cells.
   Unsupported public call forms should remain explicit adapter edges, not
   implicit fallback machinery hidden inside the hot path.

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
   compiled optimizer can inspect to decide whether it may skip protocol calls,
   inline a concrete iterator step, or leave an explicit cached `__next__` call
   in place.

   Direct builtin iteration plans are useful, but they are not a substitute for
   traceable public protocol calls. Exact immutable builtin plans may skip
   Python-visible calls only when their guards prove the same semantic result.

5. **Memory strategy and JIT root metadata**

   The intended long-term memory substrate is the generational moving collector
   described in [Generational Copying GC Design Notes](generational-copying-gc.md).
   First JIT work may still target the current deferred-refcounting substrate,
   but its root and heap-write contracts should be designed toward that collector
   rather than a generational non-moving mark-sweep design. The contracts must
   cover write barriers or temporary retain/release operations, safepoint
   records, accumulator publication, deopt metadata, transition stubs,
   native-call root publication, descriptor-driven teardown, handles or stable
   wrappers for native objects, and specialized pinning or stable-storage rules.

   The initial compiler runs on the current collector. Expanding it to
   safepoint-capable calls and compiled stores before the root and heap-write
   contracts are explicit risks rework in every call, exit, and runtime
   transition.

6. **Attribute hooks**

    Implement `__getattribute__`, `__getattr__`, `__setattr__`, and
    `__delattr__`. Function attribute reads already materialize bound methods
    for saved values such as `f = obj.m`.

7. **Class semantics cleanup**

    Replace local-slot class scopes with dynamic namespaces backed by a dict
    that supplies the class attributes once the body finishes. Synchronous
    function and class decorators are implemented, including `@staticmethod`
    and `@classmethod` on functions.

    Nested functions share lexical cells for captured bindings and support
    `nonlocal`. Uncaptured locals remain direct frame slots. Supporting
    captures across class bodies depends on the class namespace changes above.
    Extensions must preserve cell lifetime, root visibility, teardown, and
    exception propagation for uninitialized reads.
    See [Closure Cells](closure-cells.md).

8. **Generators, `yield`, and `yield from`**

    Generators create long-lived suspended frames, so they should wait until the
    memory/root model is reliable. `yield from` also needs careful interaction
    with `StopIteration.value` and internal no-value sentinels.

9. **List slice write/delete support when it becomes the shortest path**

    Keep `slice.__new__` and syntax construction validation-free: slice fields
    are raw objects, and `__index__` conversion must happen at consumption time
    because it can run Python-visible side effects.

    List slice `__setitem__` and `__delitem__`, plus any future equality/hash
    decisions, should wait until a concrete benchmark or stdlib bringup task
    needs them. Bytearray already supports slice assignment and deletion.

10. **Comprehensions and richer syntax**

    Add list/dict/set comprehensions, generator expressions, more assignment
    targets, richer string syntax, and other surface-area features after the
    runtime substrate and major semantic protocols are in better shape. See
    [Non-starred unpacking assignment](#non-starred-unpacking-assignment).

### Non-starred unpacking assignment

Implement assignment targets such as `a, b = value` and `[a, b] = value`,
including nested targets. The parser and AST represent target structure,
codegen lowers unpacking and left-to-right stores, and the interpreter and
runtime consume iterables and raise Python-visible exceptions.

The first slice excludes starred targets. It must consume generic iterables,
detect too few and too many values, and validate each unpack before beginning
its stores. With nested targets, earlier stores can precede a later nested
unpack failure. Support ordinary store targets within the unpacking target,
including attributes and subscripts. Exact tuple/list shortcuts are optional.

Done when parser, structural codegen, and interpreter tests cover tuple and list
target syntax, generic iterators, nested targets, arity failures, and store
ordering. Settle any new iteration or cache contract before implementation.

## Parked stdlib follow-ups

- `contextlib.ContextDecorator` and context-form `unittest.TestCase.assertRaises`
  remain parked pending traceback support.
- `functools.partial` remains parked pending descriptor-aware direct calls.

## Revisit Triggers

Revisit this ordering when:

- descriptor `__get__`, `__set__`, and `__delete__` execution is explicit,
  guarded, and replayable from cache metadata;
- ordinary functions, constructor thunks, direct methods, escaped bound
  methods, descriptor-produced callables, and arbitrary `__call__` objects all
  have explicit call-plan outcomes or explicit adapter edges, with direct calls
  resolving descriptors before argument evaluation;
- iteration lowering exposes `__iter__`, `__next__`, `StopIteration`
  consumption, and internal iterator plans as inspectable bytecode/cache
  metadata rather than hidden helper behavior;
- the memory strategy for broader compiled execution has an explicit contract
  for heap writes, safepoint publication, deopt/root maps, native-call root
  publication, and object pinning;
- full pystone runs in the benchmark harness and shows a different blocker than
  benchmark-source adaptation;
- implicit protocols such as `__len__`, `__iter__`, `__next__`, and numeric
  conversion have guarded dispatch plans comparable to operator dispatch;
- importlib, public finder/loader APIs, path hooks, or exact module namespace
  compatibility become necessary for broader Python source;
- public `range()` semantics become a practical blocker rather than a visible
  cleanup item;
- performance measurements show that a lower-priority item has become a
  bottleneck for existing benchmarks.
