# Project Description

This repository contains clovervm, a Python VM.

# Collaboration style

- Treat collaboration as engineering review. Challenge proposals that conflict
  with maintainability, performance evidence, Python semantics, or the codebase;
  explain the risk and what evidence would resolve uncertainty.
- Before a nontrivial new design, use `implementation-readiness-review` to agree
  on a compact sketch unless an equivalent sketch is already agreed. Keep sketch
  details in that skill rather than repeating them here.
- Complete agreed work autonomously, including verification and fixes caused by
  the change. Stop to discuss unresolved material decisions or deviations in
  public API, Python-visible semantics, error behavior, cache invalidation,
  object layout, ownership/lifetime, subsystem boundaries, or VM invariants.
  Routine implementation choices within the agreed design do not need approval.

# Architecture and layering

- Parser/AST owns syntax and source structure; codegen lowers Python-visible
  semantics into bytecode; opcode handlers execute bytecode while preserving
  dispatch shape; runtime object helpers own object semantics, allocation,
  descriptors, and type behavior. Native modules must not reach into interpreter
  frame machinery. Keep unchecked primitives free of Python-visible policy.
- Follow nearby patterns when introducing helpers, types, opcodes, AST nodes,
  cache structures, or ownership patterns. The absence of a local pattern alone
  does not require approval; a material design decision under the rule above does.
- For cross-subsystem designs, make the owning layer, existing pattern,
  preserved invariants, and verification visible in the implementation sketch.
- Prefer updating all in-repo users over adding compatibility machinery for old
  internal APIs. New metadata, fallback paths, and cross-layer shortcuts that
  change the agreed design require discussion.
- New abstractions should remove real complexity, preserve a VM invariant, or
  follow an established project pattern, rather than only reduce one or two
  local call sites.
- For builtin dunder methods and trusted handlers, use
  `clovervm-builtin-dunder-handlers` for type-specific implementation and resolver
  contracts. For interpreter dispatch changes, follow `src/runtime/AGENTS.md`.

# Changing code

- Run `clang-format -i` on every touched C++ source or header file so it matches the repository's `.clang-format`. Never run `clang-format` on `CMakeLists.txt` files.
- Use `build-debug/` for local builds. This project requires Clang for correct tail-call behavior in the interpreter (`MUSTTAIL`), so configure debug builds with `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`. Use `cmake --fresh -G Ninja -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` when a clean reconfigure is needed.
- Use `build-release/` for benchmark runs. If it is missing, configured with the wrong generator, or appears stale after dependency changes, reconfigure it with `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release`.
- For implementation or build changes, run `ninja -C build-debug all check`
  before finalizing. Use focused checks during iteration; repeat verification
  when subsequent changes affect it. Documentation/instruction-only edits need
  relevant content, link, and format checks, not a VM build.
- Before every push, run `ninja -C build-release all check` at the exact HEAD
  being pushed.
- Run benchmarks with `cmake --build build-release --target run_benchmark`.
- `ccache` is picked up at CMake configure time. If a configure or build step hits sandbox restrictions because of `ccache`, ask for elevated permissions instead of disabling `ccache` or reconfiguring the build to avoid it.
- Run git commands one at a time. Do not launch multiple git commands in parallel, because repository locking can make them fail.
- Prefer rebasing local work onto upstream changes over creating merge commits
  whenever the local commits have not been published and can be safely
  rewritten.
- Run `gh` commands with elevated privileges so GitHub authentication works.
- Prefer interpreter tests for semantics and end-to-end behavior. Keep codegen tests focused on high-value structural guarantees such as specific lowering patterns, call conventions, or optimizations that interpreter tests would not pin down well.
- When designing AST shapes for Python syntax, consult CPython's `Parser/Python.asdl` and borrow its structure where it fits clovervm before inventing a different local representation.

# Code style
- This is a C++20 code base.
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
