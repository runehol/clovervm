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

1. **Guarded operator/protocol dispatch plans and type profiling**

   The design direction for this work is captured in
   [Fast Operator Dispatch](fast-operator-dispatch.md).

   The first implementation slice should be subscription dispatch: `obj[key]`,
   `obj[key] = value`, and `del obj[key]`. It exercises the same
   special-method lookup and validity-cell machinery as overloaded operators,
   but avoids reflected and in-place candidate ordering while the cache shape is
   still being proved.

   After subscription, move other operator and implicit protocol dispatch from
   ad hoc special cases plus generic failure into explicit guarded plans. Binary
   and in-place operators such as `+` should preserve direct SMI-plus-SMI
   arithmetic as the primary hot path: the common integer case should remain a
   direct tag check and checked arithmetic path, without paying for uniform
   shape lookup or generic cache probing. The same cache shape should extend to
   other bytecode-driven dunder protocols such as numeric conversion and
   truthiness. The resolver should own Python operator semantics, including type
   special cases, special-method lookup on the type, reflected methods,
   `NotImplemented`, and eventual `TypeError` formation. Executing a Python
   `__add__` or `__radd__` should reuse the call-plan machinery as a
   special-method call, not perform ordinary instance attribute lookup.

   The fallback inline cache should use the VM's shape model as its profiling
   vocabulary, including heap-object shapes and inline-value shapes. Binary
   operator entries are keyed by operand shape pairs; other protocol caches
   should use the analogous receiver/operand shape guards and validity cells.
   Each cache entry gives both the interpreter and the future JIT a type
   profile: guarded fast paths, exact special-method call targets, validity
   cells, and a megamorphic fallback when the site stops being predictable.

2. **Slices**

   Add parse, lowering, and runtime support for `a[i:j:k]`, including
   list/tuple/string slicing and slice assignment/deletion where appropriate.
   Slices are important language surface, but should follow the subscription
   protocol-dispatch work so they can use the same `__getitem__`,
   `__setitem__`, `__delitem__`, cache, and type-profile machinery instead of
   growing a separate ad hoc path.

3. **Richer call adaptation and generic callable protocol**

   Extend the call-plan model to the remaining Python call forms: runtime
   support for positional-only parameters, callee `**kwargs`, caller `*args`,
   caller `**kwargs`, richer duplicate/error ordering, arbitrary object
   `__call__`, and native-to-managed keyword-call APIs.

   Keep this as an extension of the existing positional and keyword call-plan
   model. Do not collapse `CallPositional`, `CallKeyword`, and direct method
   calls into one broad generic slow path unless measurements show the current
   specialization is the wrong shape.

4. **Constructor semantics beyond tier-1 thunks**

   Expand constructor behavior past the current ordinary-class path. Remaining
   work includes custom `__new__`, custom metaclass `__call__`, arbitrary class
   keyword arguments, generic callable construction paths, and normalization of
   constructor failures into specific VM exceptions.

5. **Interpreter-controlled descriptor execution**

   Lookup already classifies descriptor work into plans. The next step is to
   execute `__get__`, `__set__`, and `__delete__` through explicit interpreter
   or VM-controlled dispatch so Python-visible execution, allocation, and
   exceptions are not hidden inside lookup/classification helpers.

6. **Full Python dict hashing and equality**

   Python `dict` needs arbitrary-key hashing and equality semantics rather than
   the current string-key-oriented internal assumptions. This matters for real
   Python code, imports, module namespaces, mappings, and future library work.

7. **Attribute hooks and escaped bound methods**

    Implement `__getattribute__`, `__getattr__`, `__setattr__`, and
    `__delattr__`, and add observable bound-method objects for escaped method
    values such as `f = obj.m`. Direct method-call fast paths should remain
    allocation-free when the bound method does not escape.

8. **Generators, `yield`, and `yield from`**

    Generators create long-lived suspended frames, so they should wait until the
    memory/root model is reliable. `yield from` also needs careful interaction
    with `StopIteration.value` and internal no-value sentinels.

9. **Comprehensions and richer syntax**

    Add list/dict/set comprehensions, generator expressions, more assignment
    targets, richer string syntax, and other surface-area features after the
    runtime substrate and major semantic protocols are in better shape.

## Revisit Triggers

Revisit this ordering when:

- descriptor `__get__`, `__set__`, and `__delete__` execution no longer hides
  Python-visible behavior inside lookup helpers;
- subscription dispatch has a guarded IC, and the same cache model has a clear
  path to binary/in-place operators, reflected methods, `NotImplemented`
  fallback, and analogous dunder-protocol cases;
- callee `**kwargs`, caller `*args` / `**kwargs`, positional-only parameters, or
  generic `__call__` become the next blocker for stdlib module bringup;
- importlib, public finder/loader APIs, path hooks, or exact module namespace
  compatibility become necessary for broader Python source;
- public `range()` semantics become a practical blocker rather than a visible
  cleanup item;
- performance measurements show that a lower-priority item has become a
  bottleneck for existing benchmarks.
