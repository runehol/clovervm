# Project Description

This repository contains clovervm, a Python VM.

# Collaboration style

- Treat collaboration as engineering review, not automatic agreement. Challenge assumptions when they seem wrong or under-supported.
- If the user states a preference that conflicts with maintainability, performance evidence, Python semantics, or the existing codebase, push back clearly and explain the specific risk.
- When uncertain, say what would change your mind instead of agreeing provisionally.
- Implement agreed work autonomously within its established design. Stop and ask
  before deciding a material gap the plan did not settle, especially one involving
  public API shape, Python-visible semantics, error behavior, cache invalidation,
  object layout, ownership/lifetime, cross-layer coupling, or VM invariants. Do
  not quietly add compatibility machinery, metadata, special cases, fallback
  behavior, or inferred policy to keep moving.

# Architecture and layering

- Keep behavior in the layer that owns it. Parser and AST code should describe syntax and source structure; codegen should lower Python-visible semantics into bytecode; opcode handlers should execute bytecode while preserving dispatch shape; runtime object helpers should own object semantics, allocation, descriptors, and type behavior; native modules should expose Python behavior without reaching into interpreter frame machinery.
- Before introducing a new helper, type, enum, opcode, AST node, cache structure,
  ownership pattern, or subsystem boundary, inspect nearby existing code and
  follow the closest local pattern. If no good pattern exists, treat that as a
  material design gap under the collaboration rule above.
- For changes touching more than one subsystem, first state which layer owns the behavior, which existing pattern the change follows, what invariants must remain true, and what tests or checks will prove the behavior.
- Do not solve layering friction by adding broad compatibility paths, new metadata, or cross-layer shortcuts unless that design has been explicitly agreed. In this greenfield repository, prefer updating all in-repo users to match the chosen design.
- For new builtin dunder methods and trusted handlers, keep type-specific coercion, receiver checks, result construction, and trusted resolver logic in the owning builtin type file. Use the `clovervm-builtin-dunder-handlers` project skill for the established float/int operator-template pattern.
- New abstractions must earn their weight by removing real complexity, preserving a VM invariant, or matching an established project pattern. Do not add a helper or framework solely to reduce one or two local call sites.
- Keep Python-visible semantics out of convenience code whose layer should only represent structure or mechanics, such as parser/AST plumbing, inline opcode classification helpers, or low-level unchecked primitives.
- Before finalizing a nontrivial change, check that it follows an existing local pattern, that any new abstraction is justified, that ownership and pending-exception contracts remain explicit, that Python-visible semantics are covered by interpreter-level tests where appropriate, and that verification matches the touched subsystem.

# Changing code

- Run `clang-format -i` on every touched C++ source or header file so it matches the repository's `.clang-format`. Never run `clang-format` on `CMakeLists.txt` files.
- Use `build-debug/` for local builds. This project requires Clang for correct tail-call behavior in the interpreter (`MUSTTAIL`), so configure debug builds with `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`. Use `cmake --fresh -G Ninja -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` when a clean reconfigure is needed.
- Use `build-release/` for benchmark runs. If it is missing, configured with the wrong generator, or appears stale after dependency changes, reconfigure it with `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release`.
- After making changes, run `ninja -C build-debug all check`.
- Run benchmarks with `cmake --build build-release --target run_benchmark`.
- `ccache` is picked up at CMake configure time. If a configure or build step hits sandbox restrictions because of `ccache`, ask for elevated permissions instead of disabling `ccache` or reconfiguring the build to avoid it.
- Run git commands one at a time. Do not launch multiple git commands in parallel, because repository locking can make them fail.
- Run `gh` commands with elevated privileges so GitHub authentication works.
- Prefer interpreter tests for semantics and end-to-end behavior. Keep codegen tests focused on high-value structural guarantees such as specific lowering patterns, call conventions, or optimizations that interpreter tests would not pin down well.
- When designing AST shapes for Python syntax, consult CPython's `Parser/Python.asdl` and borrow its structure where it fits clovervm before inventing a different local representation.

# Code style
- This is a C++17 code base.
- Prefer include guards over `#pragma once` in headers.
- Name include guards consistently. Follow the existing repository convention, e.g. `CL_SLAB_ALLOCATOR_H`.
- For fixed-width integer and size types, include `<cstdint>` or `<cstdlib>`/`<cstddef>` as needed and use unqualified names like `int64_t` and `size_t`.
- Prefer small non-virtual accessor definitions in headers so they are easy to inline.

# Pending exception propagation
- Functions that can set or propagate pending exception state must make that fallibility explicit in the return type. Native/interpreter boundary APIs generally return `[[nodiscard]] Value`: success is the natural result or `Value::None()`, failure is `Value::exception_marker()`. Typed/internal APIs may return `[[nodiscard]] Expected<T>`, including non-`Value` payloads such as `Expected<int32_t>`.
- This contract is transitive. `CL_PROPAGATE_EXCEPTION(...)` and `CL_TRY(...)` propagate fallibility upward; only explicit local handling may clear pending exception state. Opcode handlers must use the interpreter-specific propagation path rather than plain `CL_TRY`.
- Keep unchecked primitives free of pending-exception semantics; callers must prove validity before using them.

# Ownership semantics
- `Value` and `TValue<T>` are borrowed handles. Use them for C++ parameters and for locals whose lifetime is managed elsewhere.
- `TValue<T>` should be preferred over `Value` when the value is known to satisfy a specific semantic type, such as `String`, `SMI`, `CLInt`, or a concrete `Object` subclass.
- `Optional<T>` should be used when `None` is a valid absence state, for example `Optional<TValue<String>>`.
- `Owned<Value>` and `Owned<TValue<T>>` are RAII local owners. They retain on construction or assignment and release on destruction. Use them for local C++ variables that must keep a value alive. The same applies to composed handle types such as `Owned<Optional<TValue<T>>>`.
- `Member<Value>` and `Member<TValue<T>>` are for direct members of cl heap objects. They retain on construction or assignment and release the overwritten value on reassignment, but they do not release on destruction. This leaves the stored reference for the garbage collector to observe. The same applies to composed handle types such as `Member<Optional<TValue<T>>>`.
- Direct members of cl heap objects should use `Member<Value>`, `Member<TValue<T>>`, or another `Member<...>` handle wrapper, not `Owned<...>`.
- Prefer `Member<TValue<T>>` or `Owned<TValue<T>>` over the untyped forms when the stored value has a known type. Use the untyped forms only when the value is genuinely heterogeneous or may hold sentinels such as `None` or `not_present`.
