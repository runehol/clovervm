# Truthiness Dispatch

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Truth conversion and conditional branches, cached special-method dispatch, result validation, and Python return continuations |
| Owning layers | Builtin numeric methods and integer validation; runtime dispatch and caches; interpreter continuations; codegen and bytecode metadata |
| Validated against | `3f5095c7` plus verified implementation working tree (2026-10-12) |
| Supersedes | General truthiness dispatch entry in [Small Tasks](small-tasks.md) |

Both explicit conversions and conditional jumps implement this contract.
`ToBool`/`ToBoolNot` use embedded finishing opcodes, and Python `not` uses
`ToBoolNot`. `JumpIfTrue`/`JumpIfFalse` combine cached truth conversion and
branching, preserving the tested value on both normal edges. Native int/float
__bool__ methods, registered trusted handlers, and direct integer-length
validation are implemented. Length-result index coercion and correcting
sys.maxsize remain separate follow-ups.

## Truthiness semantics and call path

Immediate values retain their current direct truthiness test, without consulting
an inline cache. In particular, `NotImplemented` remains truthy: this deliberately
preserves the older Python behavior and avoids another check in the immediate
hot path. A `__bool__` method returning `NotImplemented` still raises TypeError,
because that method's return must be an actual boolean.

For heap values, a receiver-owned unary dispatch table selects:

1. `__bool__`, if present;
2. `__len__`, only if `__bool__` is absent;
3. default true, only if both are absent.

Use special-method lookup and the existing binding/call-adaptation machinery.
A present but non-callable or unsupported candidate must raise the relevant
call error, rather than being treated as missing. Once a method is selected,
neither an invalid return, an exception, nor `NotImplemented` triggers fallback.
There is no reflected dispatch and no arithmetic-style NotImplemented loop.

Reuse the existing OperatorInlineCache shape matching, MRO lookup validity, and
trusted-handler/Python-function replay. Add the selected result kind:

```cpp
enum class TruthResultKind { Bool, Length };

// Additional metadata in OperatorInlineCache for truthiness entries.
TruthResultKind truth_result_kind;
```

Non-truthiness entries must initialize/reset this metadata consistently without
changing their existing behavior. The cache records how to obtain and validate
the result, never the truth value of a particular object. Mutable containers
must be tested anew on every execution.

The receiver shape and MRO validity cell guard the selected method and preceding
negative lookups. Adding `__bool__` invalidates a cached length/default path;
adding `__len__` invalidates default true. The default row needs a guarded,
cacheable true handler; the existing uncached native-result path alone is not
sufficient for repeated default-object tests.

The target encoded instruction bundles are:

```text
ToBool ic       [embedded FinishTruthiness]
ToBoolNot ic    [embedded FinishTruthinessNot]
```

The main opcode emitter appends the continuation byte, following the existing
embedded CheckOperatorNotImplemented convention. Decoding, instruction length,
and ordinary disassembly treat the whole bundle as one main instruction; the
finisher is not emitted by callers or printed as a separate sequential
instruction. Its physical byte offset is still an interpreter return target.

Immediate and trusted-handler paths validate/normalize while the interpreter
retains control, then advance past the complete bundle. A Python call saves the selected
result kind in the caller's reserved continuation prefix and uses the finishing
opcode as its return address. The finisher reads this per-invocation state, not
the mutable IC: a callback can re-enter the same site and replace its cache.
The finishing path does not install cache entries or repeat method lookup.
The embedded finisher executes only on the Python return path, never by ordinary
fallthrough from a successful immediate, trusted, or default-true conversion.

Both paths use the same result-validation policy:

- Bool: require exactly True or False.
- Length: recognize bool, SMI, and BigInt directly. Validate the integer payload
  against `0 <= length <= INT64_MAX`, then test for zero. Negative lengths raise
  ValueError; lengths above the limit raise OverflowError; other types raise
  TypeError. BigInt representation alone is never an overflow verdict: boxed
  zero and one, including int-subclass values, must work.
- Apply inversion only after successful conversion for the negative opcode.
- Propagate pending exceptions through the existing interpreter frame-exit path.

There is no second IC or recursive `__bool__` invocation for length results.
Non-integer results with `__index__` are deferred and raise TypeError in this
initial protocol slice. A future index stage requires its own readiness sketch
and an explicit additional return kind; it must not recursively coerce invalid
index returns.

Use INT64_MAX as a native protocol limit, independently of mutable `sys.maxsize`.
Correcting Clover's current SMI-sized `sys.maxsize` is separate follow-up work.
The signed 64-bit protocol limit does not imply that any particular container
can allocate or represent a length that large.

## Ownership and execution boundaries

Builtin numeric types own their checked native __bool__ methods and trusted
handlers. Runtime integer conversion policy owns fallible length validation;
it inspects integer payloads without invoking Python. The existing special-method
lookup and operator walker own candidate selection and guarded call descriptors.
The interpreter owns cache replay, Python frame entry, return continuations, and
pending-exception propagation. Codegen and bytecode metadata own instruction
encoding, continuation emission, and scratch-space reservation.

Caches retain callable and validity information through the existing CodeObject
cache ownership/scanning machinery. Per-invocation continuation state uses the
existing caller-frame scratch-prefix convention; it must remain intact and
visible to frame/root scanning while Python runs. Do not retain unrooted borrowed
values across callback execution. Native modules do not access interpreter
frames.

Cache misses may install entries under the existing installation rule. Return
continuations may validate and complete the current operation, but do not install
entries or reconsider the method already selected. Callback exceptions unwind
normally, and return-validation exceptions belong to the original conversion
site. Error paths use canonical VM exception classes.

## Conditional jumps

`JumpIfTrue` and `JumpIfFalse` combine truth conversion and branching in one
instruction. Do not lower them into a conversion followed by a separate jump:
the immediate path must perform its direct bitmask test and PC adjustment with
one opcode dispatch, without consulting the cache or executing a continuation.

The accepted five-byte layouts are:

| Byte offset | JumpIfTrue bundle | JumpIfFalse bundle |
|---|---|---|
| 0 | JumpIfTrue opcode | JumpIfFalse opcode |
| 1 | OperatorInlineCache index | OperatorInlineCache index |
| 2 | FinishJumpIfTrue opcode | FinishJumpIfFalse opcode |
| 3-4 | Signed little-endian int16 displacement | Signed little-endian int16 displacement |

The displacement follows the embedded finisher so both parent and finisher can
read it directly. Its base is the end of the complete five-byte bundle,
preserving the existing end-of-displacement relocation convention. From the
parent, fallthrough is `pc + 5`; from the finisher it is `pc + 3`. A taken branch
adds the displacement to that same fallthrough address. Do not copy the branch
destination or displacement into per-invocation scratch state.

Emitters produce the complete bundle, including its cache and embedded finisher.
Decoding and ordinary disassembly expose one conditional instruction with its
cache, jump target, and fallthrough. Bytecode format metadata must describe the
cache at offset 1, continuation at offset 2, and displacement at offset 3;
existing conversion bundles have a trailing continuation, so that assumption
must not be applied to branch bundles. The finisher is a physical interpreter
return target, not an ordinary fallthrough instruction or a separate semantic
basic-block boundary. Branch targets must remain at semantic instruction
boundaries.

### Preserve the tested value

Conditional jumps preserve the original accumulator value on successful taken
and untaken branches. This is an explicit bytecode contract, shared by `if`,
`while`, conditional expressions, and short-circuit `and`/`or`. Codegen does not
insert operand save/restore instructions around jumps merely to preserve a
short-circuit expression's result. Explicit conversions still replace the
accumulator with a boolean.

The three execution paths implement that contract as follows:

- Immediate: test the existing value directly and branch, leaving it untouched.
  The cache and finisher are skipped; the displacement need only be read when
  taking the branch.
- Trusted: replay the selected unary handler and validate its result using the
  shared Bool/Length policy. Branch on that separate result while preserving the
  original operand; do not execute the embedded finisher.
- Python function: save Bool/Length and the original operand in the existing
  aligned caller scratch prefix, with call arguments below it. Return to the
  embedded finisher, validate the callback result, restore the saved operand to
  the accumulator, and branch. Restoration is required even when the caller
  does not subsequently use the operand.

The prefix already roots the receiver for the Python call. Its result kind and
original operand belong to that invocation and remain visible to frame/root
scanning. Re-entry may replace the site's IC, but the outer finisher must use
the saved kind and operand. The two finisher opcodes encode branch direction;
no additional direction flag, cache family, or second method lookup is needed.
Each execution calls at most one selected method. Invalid returns and callback
exceptions propagate without taking either normal branch, with validation
errors attributed to the original conditional jump.

The JIT retains its existing guarded immediate conditional-branch path and
preserves the operand in outgoing state. Heap operands exit to the interpreter
at the parent jump, where the cache and Python continuation execute. Do not
apply an immediate truth test to an unguarded pointer or resume a JIT exit at
the embedded finisher without its per-invocation state.

## Deferred surface

Python `not` uses the negative conversion path. Non-integer length-result index
coercion and correcting sys.maxsize remain deferred. The older immediate
NotImplemented truth behavior is an intentional policy, not a deferred error
check.
