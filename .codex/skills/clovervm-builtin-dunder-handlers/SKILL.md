---
name: clovervm-builtin-dunder-handlers
description: Use when adding, refactoring, or reviewing clovervm builtin type dunder methods, trusted method handlers, trusted operator resolvers, builtin numeric operator boilerplate, reflected operator implementations, NotImplemented behavior, or cache-replayable builtin fast paths. Applies especially to work in src/builtin_types/*.cpp that installs intrinsic methods with trusted handler resolvers.
---

# clovervm Builtin Dunder and Trusted Handlers

Use this workflow for builtin type dunder methods and trusted handlers that must
preserve Python-visible semantics while providing direct cache-replayable fast
paths.

## Layering

- Keep type-specific coercion, result construction, receiver checks, and direct
  trusted handlers in the owning builtin type file, such as
  `src/builtin_types/float.cpp` or `src/builtin_types/int.cpp`.
- Let `src/runtime/operator_dispatch.h` and `src/runtime/operator_walk.cpp`
  own protocol sequencing that already exists there: table rows, fallback order,
  reflected priority, special-method lookup, and cacheable walk descriptors.
- Do not move builtin type coercion into the operator walker, and do not make
  builtin type files reimplement protocol sequencing.
- Keep opcode handlers mechanically focused on dispatch and cache replay. Do
  not hide descriptor invocation, Python bytecode execution, allocation-heavy
  behavior, exception formatting, or general operator walking inside inline hot
  helpers.

## Preferred Implementation Shape

- Prefer the existing `float.cpp` and `int.cpp` template pattern for repeated
  builtin dunder families:
  - small operator structs that own the actual operation and receiver error;
  - native wrappers such as `native_float_binary_operator<Operator>` for direct
    dunder calls, including receiver validation and `NotImplemented`;
  - trusted wrappers such as `trusted_float_float_operator<Operator>` or
    `trusted_intlike_bigint_operator<Operator>` for already-validated cache
    replay;
  - resolver templates that map operand shape keys, operand order, and arity to
    `TrustedResolution`.
- Keep normal/reflected pairings explicit at method registration or resolver
  sites. Reflected semantics are Python-visible and should not disappear into an
  opaque generic helper.
- Keep class-specific coercion obvious. For example, float can accept float,
  SMI, and bool through double conversion; int/bigint has separate SMI, bool,
  and BigInt paths and may intentionally skip int methods for int/float pairs
  that are known to return `NotImplemented`.
- Normalize trusted handlers to the opcode operand order and replay the handler
  directly. Do not introduce trampoline calls on hot builtin method paths.
- Use `TrustedResolution::known_not_implemented_skip_method()` only when the
  builtin method is known to return `NotImplemented` for that operand shape
  combination and skipping it preserves the dispatch-table fallback order.
- If a builtin method family needs a new resolver state, cache shape,
  applicability condition, or protocol table behavior, stop and explain the
  dispatch contract before implementing it.

## Semantic Checks

- Preserve CPython-visible operator behavior: reflected priority, same-type
  dispatch, `NotImplemented` continuation, identity fallback for equality when
  applicable, unsupported-operand errors, and division/modulo error behavior.
- Preserve method-specific receiver errors for direct builtin dunder calls.
  Trusted handlers may assume their resolver proved the operand shapes, but the
  native dunder wrapper must still validate direct calls.
- Compare small semantic questions with local Python probes or CPython source
  when behavior is unclear.
- Prefer interpreter tests for observable operator behavior. Use codegen tests
  only for structural guarantees such as opcode selection, cache index layout,
  or continuation shape.
- Add cache/trusted-handler tests when the change affects inline-cache replay,
  reflected cache entries, known-`NotImplemented` skips, or validity-cell guards.

## Guardrails

- Do not make builtin dunder implementations look like generic calls when a
  direct trusted handler can preserve the same semantics.
- Do not add broad compatibility paths for stale internal APIs; update in-repo
  users to the chosen shape.
- Do not collapse type-specific numeric implementations into one abstraction
  unless it preserves readable Python semantics and the class-specific coercion
  rules remain obvious.
- For performance-sensitive operator-path changes, use release benchmarks and
  the opcode-frame checker as appropriate; debug tests prove correctness, not
  speed.
