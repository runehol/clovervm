# Interpreter code

These instructions apply to `src/runtime/`, especially
`src/runtime/interpreter.cpp`. The interpreter is hot code, and its dispatch loop
depends on Clang's `musttail` support. Treat changes there as performance- and
control-flow-sensitive.

- Preserve the opcode-handler shape. Continue to use the existing `PARAMS` /
  `ARGS`, `START`, `COMPLETE`, and `MUSTTAIL return ...` conventions unless the
  dispatch design itself is changing.
- A target of `MUSTTAIL return ...` must have exactly the `PARAMS` signature.
  Pass extra context through existing interpreter, frame, or register state, or
  through a non-tail-called helper before the `MUSTTAIL` edge.
- Keep exceptional and slow behavior in separate `NOINLINE` functions that
  opcode handlers tail-call. Use `ALWAYSINLINE` only for small, mechanical hot
  helpers.
- Do not hide Python bytecode execution, observable allocation, descriptor
  invocation, exception creation, formatting, or broad generic dispatch inside
  inline hot helpers.
- Keep custom instruction lengths explicit. If a handler cannot use `START(len)`
  directly, use a clear local such as `call_instr_len` that cannot collide with
  declarations inside `START`.
- Add new hot-path opcode handlers to
  `benchmark/hot_path_opcode_handlers.txt`; update that file when renaming a
  listed handler.
- After touching `src/runtime/interpreter.cpp`, run
  `ninja -C build-debug all check`. For frame-sensitive changes, also run
  `cmake --build build-release --target check_opcode_frames`; for broader
  performance changes, consider
  `cmake --build build-release --target run_benchmark`.
