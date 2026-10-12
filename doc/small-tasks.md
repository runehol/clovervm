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

### General truthiness dispatch

Extend conditional truthiness to heap integers and builtin strings/containers,
and to special-method dispatch through `__bool__`, then `__len__`, and default
object truth when neither exists. Keep calls and
their caches explicit for future JIT compilation. Validate result types and
negative lengths; propagate callback exceptions and lookup invalidation.
Currently non-float heap objects raise TypeError with the message
`unsupported truthiness for object`, including results of element equality during
sequence comparison. Tuple and list equality use the supported immediate/float
truthiness types; this protocol expansion is separate work requiring a readiness
sketch.

### Tuple construction and subclassing

Deferred after readiness discussion; no tuple constructor is implemented yet.
Revisit native `tuple.__new__` with empty construction, exact tuple identity reuse,
direct exact-list conversion, and fresh native storage for subclass targets.
Subclass storage must use the requested class root shape, zero inline capacity,
and common overflow attributes. Copy and retain element references, not elements.

General iterable conversion remains an unsettled boundary: there is no existing
general C++ iterable-consumption helper, and invoking a retained Python helper
to materialize a temporary list was discussed but not accepted for implementation.
Tuple/list subclass inputs can override `__iter__`; do not bypass those callbacks
by copying their native payload unconditionally. Agree on the first supported
input slice and fallback design before coding.

Verify argument/error behavior, exact and subclass result identity, element
lifetime, iteration overrides, attributes, operation results, and constructor
invalidation. Tuple equality is implemented separately. This item remains deferred
rather than the next active native-constructor stage.

### Complete small `operator` helpers

Implement one helper at a time where the underlying VM operation already
exists. Check `stdlib/operator.py` before starting because this list can become
stale as the module grows. Likely candidates are exact `operator.call`, a
better `length_hint`, and the remaining sequence helpers whose required
protocols are already available.

Done when the helper has focused stdlib tests and does not claim compatibility
for protocol behavior the VM cannot yet execute.

## Bounded, But Larger

### Follow-ups from native constructor work

- **Text/bytes encoding:** implement `str.encode`, `bytes.decode`, and the
  encoding/errors forms of `bytes(text, encoding, errors)` and
  `str(data, encoding, errors)`. Choose the first codecs and error policies in a
  separate readiness sketch.
- **Integer/bytes conversion:** implement `bytes(count)` zero filling, then
  `int.to_bytes` and `int.from_bytes` as separate slices. Cover byte order,
  signedness, length/range errors, and the applicable integer protocols.
- **Bytes conversion protocols:** add `__bytes__`, generic iterable input,
  element `__index__`, and buffer inputs when their dispatch/storage contracts
  are ready. Bytes subclasses can override `__bytes__`; the current native
  bytes-input shortcut does not honor this yet. Keep this separate from native
  subclass allocation. Also distinguish an omitted bytes source from explicit
  `None`: the current default uses `None` and accepts `bytes(None)` as empty.
- **Native exception lookup:** stop resolving internal exception classes through
  mutable public builtin bindings. Rebinding `builtins.TypeError` can currently
  trigger a debug assertion when native code raises an error. Keep this separate
  from the private bindings used by Python sequence helpers.
- **Numeric conversion coverage:** float string parsing and user-defined
  `__float__` / `__index__` conversion remain deferred. Int conversion protocols
  and additional constructor forms need their own semantic sketches too.
- **Closure capture for builtin helpers:** once
  [closure cells](closure-cells.md) are implemented, replace persistent private
  bindings in the builtin implementation module with captured builtin
  classes/functions where appropriate.
- **Combined `__new__` / `__init__`:** retain original call arguments and bind
  the two methods independently, then check the returned type and resolve its
  initializer after allocation. General call unpacking/forwarding is prerequisite
  work; handle initializer replacement during allocation and coordinated default
  method argument rules. See the current
  [constructor limitations](native-subtype-storage.md#constructor-limitations).

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
