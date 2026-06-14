# BigInt Staging Plan

This plan breaks the accepted BigInt design into implementation stages. The
design rationale and semantic boundaries live in [BigInt Design](bigint.md).

## Implementation Items

### Stage 1: Representation And Registration

- [ ] Add `src/builtin_types/bigint.h` and `src/builtin_types/bigint.cpp`.
- [ ] Define `digit_t`, `signum_t`, `BigIntView`, `MutableBigIntView`,
      `SmiBigInt`, `BigIntScratch`, and `BigInt`.
- [ ] Add `NativeLayoutId::BigInt`.
- [ ] Add `BigInt` to the native layout registry.
- [ ] Register `NativeLayoutId::BigInt` to the existing `int` class.
- [ ] Add build-system entries for the new BigInt source file.
- [ ] Add focused tests for native layout descriptors, exact-sized allocation,
      and `SmiBigInt` view edge cases.

### Stage 2: Conversion, Parsing, And Formatting

- [ ] Implement `BigInt::view()`.
- [ ] Implement `SmiBigInt` construction from decoded SMI-range integers.
- [ ] Implement `BigIntScratch` inline storage plus `std::vector<digit_t>`
      overflow backing.
- [ ] Implement result finalization from `BigIntView` to SMI or exact-sized
      heap `BigInt`.
- [ ] Implement full `int64_t` to/from BigInt conversion, including
      `INT64_MIN`.
- [ ] Implement decoded SMI-range conversion helpers with range assertions.
- [ ] Implement decimal formatting for BigInt `str()` and `repr()`.
- [ ] Extend `int(str)` overflow handling to parse into BigInt while preserving
      current whitespace, sign, and underscore grammar.
- [ ] Add tests for `INT64_MIN`, `-1`, SMI boundaries, decimal round trips, and
      malformed literals.

### Stage 3: Integer Categories And Comparisons

- [ ] Add explicit helpers for intlike, exact-int, and SMI-sized integer
      classification/conversion.
- [ ] Audit sites that use `is_integer()` followed by `get_smi()`.
- [ ] Update checked SMI-sized internal conversions to reject out-of-SMI
      BigInts with `OverflowError`.
- [ ] Implement equality comparisons across bool, SMI, and BigInt.
- [ ] Implement ordering comparisons across bool, SMI, and BigInt.
- [ ] Leave BigInt hashing deferred while dictionaries only support string
      keys.
- [ ] Add tests for bool/SMI/BigInt comparison behavior and SMI-sized boundary
      rejection.

### Stage 4: Basic Arithmetic Dunder And Trusted Handlers

- [ ] Implement BigInt addition kernels using `BigIntView` inputs and
      `BigIntScratch` destination storage.
- [ ] Implement BigInt subtraction kernels.
- [ ] Implement BigInt multiplication kernels.
- [ ] Implement unary plus and unary negation.
- [ ] Update non-trusted builtin int dunder methods for bool, SMI, and BigInt
      operand combinations.
- [ ] Update trusted builtin int handlers for bool, SMI, and BigInt operand
      combinations.
- [ ] Ensure all arithmetic results finalize to SMI when representable.
- [ ] Add tests for SMI overflow promotion, mixed SMI/BigInt arithmetic,
      cancellation to SMI, sign handling, and unary negation of
      `value_smi_min`.

### Stage 5: Opcode Overflow Routing

- [ ] Change SMI fast opcode overflow paths for promoted operations to
      tail-call ordinary operator dispatch.
- [ ] Keep BigInt arithmetic out of opcode handlers.
- [ ] Preserve the interpreter opcode handler `PARAMS`, `ARGS`, `START`,
      `COMPLETE`, and `MUSTTAIL return ...` conventions.
- [ ] Add interpreter tests showing SMI overflow expressions now produce BigInt
      results.
- [ ] Confirm non-overflow SMI fast-path behavior remains unchanged.

## Invariants

- BigInt heap objects are exact-sized Python-visible objects, not temporary
  arithmetic work buffers.
- BigInt heap objects contain no owned `Value` cells beyond the inherited
  `Object` shape field.
- BigInt storage is sign-magnitude with little-endian 32-bit digits.
- Zero is canonical: `signum == 0` and `n_digits == 0`.
- Nonzero BigInts have no high zero digits.
- VM-created public integer results return SMI whenever representable.
- `SmiBigInt` takes decoded SMI-range integers, never tagged SMI bits.
- Bool normalization happens in int operand adapters, not inside the BigInt
  class.
- `BigIntView` is read-only and has no capacity.
- `MutableBigIntView` is destination storage, has capacity, and can convert to
  `BigIntView`.
- Public arithmetic kernels assume destination storage does not alias any input
  view.
- `BigIntScratch` digit storage is uninitialized; kernels initialize the ranges
  they use.
- SMI opcode overflow should enter ordinary operator dispatch so inline caches
  can record the selected BigInt path.
- Opcode handlers should not directly implement BigInt arithmetic.
- `int(str)` should preserve existing accepted grammar while routing overflow
  to BigInt parsing.
- List, tuple, string, and slice internals remain SMI-sized in the first
  implementation slice.
- BigInt hashing is deferred until non-string dictionary keys are supported.

## Verification

- Run `clang-format -i` on every touched C++ source or header file.
- Run `ninja -C build-debug all check` after each implementation stage.
- After touching `src/runtime/interpreter.cpp`, verify the normal debug checks
  still cover the interpreter `musttail`/frame constraints.
