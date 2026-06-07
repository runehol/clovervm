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

1. **Non-inline comparison specialization and guarded protocol dispatch**

   The design direction for this work is captured in
   [Fast Operator Dispatch](fast-operator-dispatch.md).

   Subscription dispatch has now proved the basic shape: the inline cache can
   guard the ordinary Python special-method lookup result, then install a
   trusted native handler based on receiver and operand/key shapes. The next
   priority is to apply the same model to comparisons and other non-inline
   operands, where exact builtin types can avoid generic dispatch without
   changing Python-visible semantics.

   The first concrete step should be exact `str` comparison:

   - generic Python-visible `str ==`, `str !=`, `<`, `<=`, `>`, and `>=`;
   - trusted exact `str`/`str` handlers selected by operand shapes;
   - comparison microbenchmarks that cover equal strings, early differences,
     and long common prefixes;
   - enough ordering support to unblock full pystone.

   After comparisons, move other operator and implicit protocol dispatch from
   ad hoc special cases plus generic failure into explicit guarded plans. Binary
   and in-place operators such as `+` should preserve direct SMI-plus-SMI
   arithmetic as the primary hot path: the common integer case should remain a
   direct tag check and checked arithmetic path, without paying for uniform
   shape lookup or generic cache probing. The resolver should own Python
   operator semantics, including type special cases, special-method lookup on
   the type, reflected methods, `NotImplemented`, and eventual `TypeError`
   formation. Executing a Python `__add__` or `__radd__` should reuse the
   call-plan machinery as a special-method call, not perform ordinary instance
   attribute lookup.

   The fallback inline cache should use the VM's shape model as its profiling
   vocabulary, including heap-object shapes and inline-value shapes. Binary
   operator entries are keyed by operand shape pairs; other protocol caches
   should use the analogous receiver/operand shape guards and validity cells.
   Each cache entry gives both the interpreter and the future JIT a type
   profile: guarded fast paths, exact special-method call targets, validity
   cells, and a megamorphic fallback when the site stops being predictable.

   Recent constructor benchmarks reinforce that this is not only an operator
   issue. Once `str(int)` bypassed generic `__str__` lookup, the remaining
   benchmark pressure moved to general VM work: cached calls, Python-level
   `len()` lowering through `__clover_call_special__`, and short-lived string
   allocation/reclamation. The next protocol-dispatch slice should therefore
   include cached dunder method calls for implicit protocols such as `__len__`,
   `__iter__`, `__next__`, and numeric conversions, not just arithmetic
   operators.

2. **Full pystone benchmark**

   The current `pystone_lite.py` benchmark exercises a useful subset, but the
   full Python 3-era pystone benchmark is now close enough to be valuable. The
   benchmark harness already has external CPython timing through
   `cpython_runner.py`, and Google Benchmark externally times CloverVM
   `run(n)`, so full pystone should be adapted into the same harness rather
   than using pystone's internal timing.

   The remaining semantic blocker is string ordering: full pystone compares
   characters and strings with `<`, `<=`, `>`, and `>=`. Setup-only unsupported
   constructs such as list comprehensions, list multiplication, and unpacking
   assignment can be rewritten in the benchmark source without changing the
   measured loop body. String ordering should not be rewritten to `ord(...)` in
   the benchmark body, because that changes the workload.

3. **Slice write/delete support when it becomes the shortest path**

   Keep `slice.__new__` and syntax construction validation-free: slice fields
   are raw objects, and `__index__` conversion must happen at consumption time
   because it can run Python-visible side effects.

   List slice `__setitem__` and `__delitem__`, plus any future equality/hash
   decisions, should wait until a concrete benchmark or stdlib bringup task
   needs them.

4. **Richer call adaptation and generic callable protocol**

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

5. **Interpreter-controlled descriptor execution**

   Lookup already classifies descriptor work into plans. The next step is to
   execute `__get__`, `__set__`, and `__delete__` through explicit interpreter
   or VM-controlled dispatch so Python-visible execution, allocation, and
   exceptions are not hidden inside lookup/classification helpers.

6. **Full Python dict hashing and equality**

   Python `dict` needs arbitrary-key hashing and equality semantics rather than
   the current string-key-oriented internal assumptions. This matters for real
   Python code, imports, module namespaces, mappings, and future library work.

7. **Short-lived allocation and reclamation pressure**

    Repeated constructor/conversion and get-slice benchmarks now spend
    significant time allocating and reclaiming tiny temporary objects, especially
    strings and short list/tuple slice results. Before treating formatting,
    protocol dispatch, or slice copy loops as the primary remaining bottleneck,
    measure whether the benchmark has become an allocator/reclamation workload.
    Candidate work includes improving zero-count-table processing, slab reuse,
    and size-class behavior for very small short-lived objects.

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

10. **Remaining basic operators**

    Fill in the parsed-but-not-yet-executable basic operators once the higher
    priority operator/protocol dispatch design is settled enough that they do
    not become another ad hoc semantic path.

    Known gaps include `**` / power, `&`, `|`, `^`, unary `~`, `@`, `in`, and
    `not in`. Some of these already have parser and codegen surface, such as
    `Pow`, bitwise bytecodes, and containment test bytecodes, but either lack
    interpreter handlers or lack a real bytecode mapping. The `operator` module
    mirrors the same gap for `pow`, `matmul`, bitwise functional helpers, and
    `invert`.

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
- exact string comparisons have generic semantics, trusted `str`/`str`
  handlers, and benchmark coverage;
- full pystone runs in the benchmark harness and shows a different blocker than
  string ordering or benchmark-source adaptation;
- comparison dispatch has a guarded IC, and the same cache model has a clear
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
