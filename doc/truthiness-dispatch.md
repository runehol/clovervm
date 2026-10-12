# Truthiness Dispatch

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Partial |
| Scope | Truth conversion semantics, cached special-method dispatch, result validation, and Python return continuations |
| Owning layers | Builtin numeric methods and integer validation; runtime dispatch and caches; interpreter continuations; codegen and bytecode metadata |
| Validated against | `856bf6c4` (2026-10-12) |
| Supersedes | General truthiness dispatch entry in [Small Tasks](small-tasks.md) |

This is the agreed target contract. Both `ToBool` and `ToBoolNot` implement the
cached truthiness table and their embedded return continuations. Python `not`
uses `ToBoolNot`; the duplicate `Not` opcode has been retired. Native int/float
__bool__ methods, registered trusted handlers, and direct integer-length
validation are implemented. Conditional jumps still support immediate values
and floats; their protocol integration remains in the final stage of the
[implementation plan](truthiness-implementation-plan.md).

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

## Conditional jumps and deferred surface

Conditional jumps will reuse this dispatch and validation policy in a subsequent
slice. They must retain the original operand when needed for short-circuit
expression results. The branch-specific continuation encoding needs a readiness
review before implementation; it is not fixed by this contract.

Python `not` uses the negative conversion path. Non-integer length-result index
coercion, conditional-jump integration, and correcting sys.maxsize remain deferred
until their planned slices. The older immediate NotImplemented truth behavior is
an intentional policy, not a deferred error check.
