# Small Tasks

This is a queue of bounded tasks for times when taking on a major architecture
item is not practical. It is deliberately separate from
[Development Priorities](development-priorities.md), which records dependency
order for larger work.

Tasks belong here when the intended Python-visible behavior is already clear,
the implementation follows existing repository patterns, and the work can
reasonably land as one focused change. Remove tasks when they are completed or
when they grow into design work; this is a work queue, not a feature history.

## Ready

### Float hashing

Implement hashing for exact floats and float subclasses. Equal numeric values
must have equal hashes across `float`, `int`, and `bool`; cover integral and
fractional values, signed zero, infinities, NaNs, and large integers. Pin the
behavior against CPython before choosing the hash implementation. Add dictionary
and set lookup regressions as well as direct `hash()` tests.

### Tuple and list equality

Implement elementwise `__eq__` and `__ne__` for both types. Both are missing:
separately constructed `(1, 2)` tuples and `[1, 2]` lists currently compare
unequal, as do empty containers. Cover length mismatches, identity shortcuts for
elements, subclass behavior, incompatible sequence types, `NotImplemented`, and
exceptions from element comparisons. List comparisons also need tests for
mutation and re-entry during equality callbacks. Keep Python callbacks on the
existing managed dispatch path; settle the continuation design before coding.

### Conditional expressions

Support `body if test else alternative`. The parser currently rejects this
syntax. Use CPython's `IfExp` AST structure where it fits, lower it to existing
branches, and evaluate only the selected expression. Cover precedence, nested
expressions, truthiness callbacks, side effects, and exception propagation with
parser and interpreter tests.

### Fill narrow builtin constructor gaps

Add one builtin constructor at a time where the allocation/conversion policy is
already settled and can be expressed as that type's intrinsic `__new__`. Keep
each constructor as its own change. Candidate slices include:

- `tuple()` for the empty case before iterable conversion is attempted.

Numeric float construction and empty/iterable list construction are implemented;
see the [native subtype implementation plan](native-subtype-implementation-plan.md).

Do not use these slices to invent a general constructor fallback or silently
accept inputs whose conversion protocol is not implemented. Each slice is done
when its Python-visible arity, result type, supported conversions, and errors
have interpreter tests.

### Complete small `operator` helpers

Implement one helper at a time where the underlying VM operation already
exists. Check `stdlib/operator.py` before starting because this list can become
stale as the module grows. Likely candidates are exact `operator.call`, a
better `length_hint`, and the remaining sequence helpers whose required
protocols are already available.

Done when the helper has focused stdlib tests and does not claim compatibility
for protocol behavior the VM cannot yet execute.

### Accept `bool` in SMI-sized integer consumers

Audit one public API that directly rejects `bool` despite Python treating it as
an `int`, starting with `range()`. Route the conversion through the existing
intlike policy where the target representation remains SMI-sized; do not fold
arbitrary-size `BigInt` support into the same patch.

Done when interpreter tests cover `False`, `True`, ordinary SMI values, and the
existing out-of-range or unsupported-value behavior.

## Bounded, But Larger

### Follow-ups from native constructor work

- **Native exception lookup:** stop resolving internal exception classes through
  mutable public builtin bindings. Rebinding `builtins.TypeError` can currently
  trigger a debug assertion when native code raises an error. Keep this separate
  from the private bindings used by Python sequence helpers.
- **Numeric conversion coverage:** float string parsing and user-defined
  `__float__` / `__index__` conversion remain deferred. Int conversion protocols
  and additional constructor forms need their own semantic sketches too.
- **Closure capture for builtin helpers:** once
  [closure cells](closure-cells.md) are implemented, replace persistent private
  bootstrap globals with captured builtin classes/functions where appropriate.
- **Combined `__new__` / `__init__`:** retain original call arguments and bind
  the two methods independently, then check the returned type and resolve its
  initializer after allocation. Track dependencies and remaining constructor
  stages in the [implementation plan](native-subtype-implementation-plan.md#deferred-work).

Exception allocation and native initializers are already implemented; their old
queue entry has been retired. These follow-ups are tracked here, but need a
readiness sketch before implementation where the owning design is unsettled.

### Non-starred unpacking assignment

Implement assignment targets such as `a, b = value` and `[a, b] = value`,
including nested non-starred targets. The behavior belongs across the existing
layers: parser/AST represents target structure, codegen lowers unpacking and
left-to-right stores, and interpreter/runtime machinery performs iterable
unpacking with Python-visible exceptions.

The first slice should exclude starred targets. It must still consume a generic
iterable, detect too few and too many values, avoid partially storing targets
when unpacking itself fails, and support assignment targets wherever ordinary
stores are already valid. Exact tuple/list shortcuts are optional optimizations,
not the semantic implementation.

Done when parser, structural codegen, and interpreter tests cover tuple and list
target syntax, generic iterators, nested targets, arity failures, and store
ordering. If the implementation needs a new iteration or cache contract beyond
the existing special-method call pattern, move it out of this list and settle
that design first.
