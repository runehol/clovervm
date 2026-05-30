---
name: python-semantics-design
description: Design or review clovervm features against Python-visible semantics. Use when a request asks how Python or CPython handles syntax, calls, descriptors, binding, object construction, exceptions, evaluation order, bytecode behavior, or other language semantics, then needs a clovervm design, plan, or implementation boundary.
---

# Python Semantics Design

Use this workflow before implementing or approving clovervm behavior that may be visible to Python code.

## Workflow

1. State the Python-visible question precisely.

Identify what user code could observe:

- evaluation order and side effects
- exception type, timing, and message shape when important
- descriptor lookup, binding, method calls, metaclass behavior, or special methods
- scope/binding rules such as local/global/nonlocal, definite assignment, and deletion
- return values, mutation, identity, and object lifetime observability
- syntax/AST shape and bytecode lowering

If the request is only about an internal optimization, still name the semantic contract it must preserve.

2. Establish CPython behavior from primary evidence.

Prefer local Python probes for small semantic questions:

```bash
python3 - <<'PY'
# minimal reproducer here
PY
```

For syntax, AST, binding, bytecode, or interpreter details, inspect the relevant CPython source/docs. Useful starting points:

```text
Parser/Python.asdl
Python/bytecodes.c
Python/ceval.c
Python/compile.c
Python/symtable.c
Objects/typeobject.c
Objects/descrobject.c
Objects/call.c
Lib/dis.py
Lib/test/
```

Use browsing only when local sources are unavailable or the exact current upstream behavior matters. Prefer CPython docs/source over secondary explanations.

3. Inspect clovervm's current representation.

Find the existing path before proposing a new one:

- parser and AST representation
- symbol/binding analysis
- codegen and bytecode format
- interpreter opcode handlers and slow paths
- object model, shapes, descriptors, classes, and metaclasses
- inline cache and validity-cell machinery
- tests that already encode nearby semantics

Do not assume CPython's internal architecture should be copied directly. Copy the semantic shape; adapt the implementation to clovervm's existing invariants.

4. Separate semantic requirements from optimization choices.

Write down:

- required behavior: what must be true for Python programs
- allowed simplifications: unsupported features or conservative slow paths
- optimization candidates: inline caches, fused opcodes, thunks, specialized bytecode, or analysis results
- invalid designs: approaches that would change observable behavior

For greenfield clovervm work, do not add backwards-compatibility layers unless existing repo uses require them.

5. Choose the implementation boundary deliberately.

Default placement heuristics:

- Parser/AST: syntax shape, source locations, incomplete-input metadata.
- Binding/flow pass: lexical scope, definite presence, global/nonlocal, local deletion checks.
- Codegen: lowering choices, register layout, opcode selection, structural guarantees.
- Interpreter: runtime dispatch, dynamic lookup, pending exception propagation, hot-path-sensitive behavior.
- Object model: descriptor semantics, class/metaclass rules, shape and validity invalidation.
- Stdlib/Python layer: policy that is expressible without VM changes.

If the design exposes a significant gap in public API shape, error behavior, cache invalidation, object layout, ownership/lifetime, or cross-cutting VM invariants, stop and ask rather than silently inventing new policy.

6. Pin behavior with the right tests.

Prefer interpreter/Python tests for Python-visible semantics. Use codegen tests only for structural guarantees such as opcode selection, register layout, call convention, or an optimization that interpreter tests cannot pin down.

Include adversarial tests for:

- side effects during lookup, argument evaluation, or binding
- custom descriptors, metaclasses, `__getattribute__`, `__getattr__`, `__new__`, `__init__`, or special methods when relevant
- deletion/redefinition and undefined local/global paths
- slow-path fallback after a cache miss or invalidation
- unsupported cases that should fail honestly

7. Report the decision in review form.

For design discussion, lead with:

- CPython/Python behavior
- current clovervm state
- recommended design
- semantic risks and rejected alternatives
- test plan

For implementation, summarize any intentional deviations or unsupported behavior in the final response.
