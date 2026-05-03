# Project Description

This repository contains clovervm, a Python VM.

# Collaboration style

- Act as a senior engineering collaborator, not a rubber stamp. Challenge the user's assumptions when they seem wrong or under-supported.
- If the user states a preference that conflicts with maintainability, performance evidence, Python semantics, or the existing codebase, push back clearly and explain the specific risk.
- When uncertain, say what would change your mind instead of agreeing provisionally.

# Changing code

- Run `clang-format -i` on every touched C++ source or header file so it matches the repository's `.clang-format`. Never run `clang-format` on `CMakeLists.txt` files.
- Use `build-debug/` for local builds. This project requires Clang for correct tail-call behavior in the interpreter (`MUSTTAIL`), so configure debug builds with `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`. Use `cmake --fresh -G Ninja -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` when a clean reconfigure is needed.
- Use `build-release/` for benchmark runs. If it is missing, configured with the wrong generator, or appears stale after dependency changes, reconfigure it with `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release`.
- After making changes, run `ninja -C build-debug all check`.
- Run benchmarks with `cmake --build build-release --target run_benchmark`.
- `ccache` is picked up at CMake configure time. If a configure or build step hits sandbox restrictions because of `ccache`, ask for elevated permissions instead of disabling `ccache` or reconfiguring the build to avoid it.
- Run git commands one at a time. Do not launch multiple git commands in parallel, because repository locking can make them fail.
- Prefer interpreter tests for semantics and end-to-end behavior. Keep codegen tests focused on high-value structural guarantees such as specific lowering patterns, call conventions, or optimizations that interpreter tests would not pin down well.
- This is a greenfield project. Backwards compatibility layers or fallbacks are generally not needed when all uses inside the git repository can be updated.
- When designing AST shapes for Python syntax, consult CPython's `Parser/Python.asdl` and borrow its structure where it fits clovervm before inventing a different local representation.

# Interpreter code

`src/interpreter.cpp` is unusually sensitive: it is hot code, and the dispatch loop depends on Clang's `musttail` support. Treat changes there as performance- and control-flow-sensitive.

- Preserve the opcode-handler shape. Handlers should continue to use the existing `PARAMS` / `ARGS`, `START`, `COMPLETE`, and `MUSTTAIL return ...` conventions unless the dispatch design itself is being changed.
- Functions that are targets of `MUSTTAIL return ...` must have exactly the `PARAMS` signature. Do not add extra parameters, omit parameters, reorder parameters, or wrap the signature in a variant form; Clang's `musttail` requires the caller and callee signatures to match exactly. Pass any extra context through existing interpreter state, frame/register state, or a non-tail-called helper before the `MUSTTAIL` edge.
- Keep exceptional and slow paths out of inlineable hot helpers. Raising C++ exceptions, formatting messages, and other cold behavior should live in separate `NOINLINE` functions that opcode handlers tail-call.
- Use `ALWAYSINLINE` only for small, mechanical helpers that avoid duplication in hot paths, such as frame setup, register-span interpretation, or simple descriptor classification. Do not hide large semantic operations behind inline helpers.
- Avoid adding broad generic dispatch layers in front of common opcodes unless there is a measured reason or the change is part of an explicit interpreter/JIT design step. Prefer explicit branches in the opcode handler when that keeps the hot path obvious.
- Be careful with helper calls from opcode handlers: anything that may run Python bytecode, allocate observably, invoke descriptors, or raise should happen through an explicit opcode slow path or a clearly cold helper, not from lookup/classification code that is meant to be inlineable.
- Keep instruction length handling explicit when an opcode has custom control flow. If a handler cannot use `START(len)` directly, use a clearly named local such as `call_instr_len`; avoid names that collide with `START`'s internal declarations.
- `tools/check_opcode_frames.py` checks that release-build hot-path opcode handlers enter without setting up stack frames. It is wired into `benchmark/check_opcode_frames` and `benchmark/run_benchmark` through CMake; when debugging it directly, pass the release `clovervm` binary and `benchmark/hot_path_opcode_handlers.txt` as the required list.
- If you add an opcode, consider whether its handler is on the interpreter hot path. Add hot-path handlers to `benchmark/hot_path_opcode_handlers.txt` so the stack checker protects them. If you rename an opcode handler, update the same list in the same change.
- After touching `src/interpreter.cpp`, build with Clang through `build-debug/` and run `ninja -C build-debug all check`. For performance-sensitive refactors, also consider `cmake --build build-release --target run_benchmark`.

# Code style
- This is a C++17 code base.
- Prefer include guards over `#pragma once` in headers.
- Name include guards consistently. Follow the existing repository convention, e.g. `CL_SLAB_ALLOCATOR_H`.
- For fixed-width integer and size types, include `<cstdint>` or `<cstdlib>`/`<cstddef>` as needed and use unqualified names like `int64_t` and `size_t`.
- Prefer small non-virtual accessor definitions in headers so they are easy to inline.

# Pending exception propagation
- Functions that can set or propagate pending exception state must return `[[nodiscard]] Value`: success is the natural result or `Value::None()`, failure is `Value::exception_marker()`.
- This contract is transitive. `CL_PROPAGATE_EXCEPTION(...)` propagates fallibility upward; only explicit local handling may clear pending exception state.
- Keep unchecked primitives free of pending-exception semantics; callers must prove validity before using them.

# Ownership semantics
- `Value` and `TValue<T>` are borrowed handles. Use them for C++ parameters and for locals whose lifetime is managed elsewhere.
- `TValue<T>` should be preferred over `Value` when the value is known to satisfy a specific semantic type, such as `String`, `SMI`, `CLInt`, or a concrete `Object` subclass.
- `OwnedValue` and `OwnedTValue<T>` are RAII local owners. They retain on construction or assignment and release on destruction. Use them for local C++ variables that must keep a value alive.
- `MemberValue` and `MemberTValue<T>` are for direct members of cl heap objects. They retain on construction or assignment and release the overwritten value on reassignment, but they do not release on destruction. This leaves the stored reference for the garbage collector to observe.
- Direct members of cl heap objects should use `MemberValue` or `MemberTValue<T>`, not `OwnedValue` or `OwnedTValue<T>`.
- Prefer `MemberTValue<T>` or `OwnedTValue<T>` over the untyped forms when the stored value has a known type. Use the untyped forms only when the value is genuinely heterogeneous or may hold sentinels such as `None` or `not_present`.
