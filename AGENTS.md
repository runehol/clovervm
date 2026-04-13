# Project Description

This repository contains clovervm, a Python VM.

# Changing code

- Run `clang-format -i` on every touched C++ source or header file so it matches the repository's `.clang-format`.
- Use `build-debug/` for local builds. If it is missing, configured with the wrong generator, or appears stale after dependency changes, reconfigure it with `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`. Use `cmake --fresh -G Ninja -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug` when a clean reconfigure is needed.
- Use `build-release/` for benchmark runs. If it is missing, configured with the wrong generator, or appears stale after dependency changes, reconfigure it with `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release`.
- After making changes, run `ninja -C build-debug all check`.
- Run benchmarks with `cmake --build build-release --target run_benchmark`.
- `ccache` is picked up at CMake configure time. If a configure or build step hits sandbox restrictions because of `ccache`, ask for elevated permissions instead of disabling `ccache` or reconfiguring the build to avoid it.
- Run git commands one at a time. Do not launch multiple git commands in parallel, because repository locking can make them fail.
- Prefer interpreter tests for semantics and end-to-end behavior. Keep codegen tests focused on high-value structural guarantees such as specific lowering patterns, call conventions, or optimizations that interpreter tests would not pin down well.

# Code style
- This is a C++17 code base.
- Prefer include guards over `#pragma once` in headers.
- Name include guards consistently. Follow the existing repository convention, e.g. `CL_SLAB_ALLOCATOR_H`.
- For fixed-width integer and size types, include `<cstdint>` or `<cstdlib>`/`<cstddef>` as needed and use unqualified names like `int64_t` and `size_t`.
- Prefer small non-virtual accessor definitions in headers so they are easy to inline.

# Ownership semantics
- `Value` and `TValue<T>` are borrowed handles. Use them for C++ parameters and for locals whose lifetime is managed elsewhere.
- `TValue<T>` should be preferred over `Value` when the value is known to satisfy a specific semantic type, such as `String`, `SMI`, `CLInt`, or a concrete `Object` subclass.
- `OwnedValue` and `OwnedTValue<T>` are RAII local owners. They retain on construction or assignment and release on destruction. Use them for local C++ variables that must keep a value alive.
- `MemberValue` and `MemberTValue<T>` are for direct members of cl heap objects. They retain on construction or assignment and release the overwritten value on reassignment, but they do not release on destruction. This leaves the stored reference for the garbage collector to observe.
- Direct members of cl heap objects should use `MemberValue` or `MemberTValue<T>`, not `OwnedValue` or `OwnedTValue<T>`.
- Prefer `MemberTValue<T>` or `OwnedTValue<T>` over the untyped forms when the stored value has a known type. Use the untyped forms only when the value is genuinely heterogeneous or may hold sentinels such as `None` or `not_present`.
