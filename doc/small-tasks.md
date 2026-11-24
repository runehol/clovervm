# Small Tasks

This is a queue of bounded tasks for times when taking on a major architecture
item is not practical. It is deliberately separate from
[Development Priorities](development-priorities.md), which records dependency
order for larger work.

Tasks belong here when the intended Python-visible behavior is already clear,
the implementation follows existing repository patterns, and the work can
reasonably land as one focused change. Remove tasks when they are completed or
when they grow into design work; this is a work queue, not a feature history.

## Bounded, But Larger

### Follow-ups from native constructor work

- **Text/bytes constructor overloads.** Add the encoding/errors forms of
  `bytes(text, encoding, errors)` and `str(data, encoding, errors)`. The
  existing `str.encode` and `bytes.decode` methods cover strict one-shot UTF-8
  and Latin-1. Settle the constructor input and dispatch rules in a readiness
  sketch.
- **Integer/bytes conversion.** `int.to_bytes` and `int.from_bytes` are still
  candidates for separate slices. Cover byte order, signedness, length/range
  errors, and the applicable integer protocols.
- **Bytes conversion protocols.** Construction from list and tuple inputs,
  including subclasses, currently reads stored elements and accepts exact
  integers in 0–255. Generic iterables and list/tuple subclass iteration
  overrides still need support. Track `__bytes__` dispatch (including
  overrides on bytes subclasses), custom element `__index__`, and general
  buffer inputs as open work.
- **Numeric conversion coverage:** user-defined `__float__` / `__index__`
  conversion remains deferred. Int conversion protocols
  and additional constructor forms need their own semantic sketches too.
- **Correct `sys.maxsize`.** It currently exposes the SMI maximum. Expose
  INT64_MAX on the current 64-bit targets and audit defaults captured at
  definition time (`list.index` and `tuple.index`) and workarounds in
  `operator` and `heapq`. Truthiness length validation already uses the native
  signed 64-bit limit. See the
  [truthiness architecture contract](truthiness-dispatch.md).
- **Builtin helper binding audit.** With [closure cells](closure-cells.md)
  available, audit which private bindings in the builtin implementation module
  could become lexical captures. Record when each binding resolves and what
  rebinding it currently observes. Propose captures only where those semantics
  are preserved.
- **Combined `__new__` / `__init__`:** retain original call arguments and bind
  the two methods independently, then check the returned type and resolve its
  initializer after allocation. General call unpacking/forwarding is now
  available; retaining the original argument containers across allocation and
  independently binding both methods still needs its own readiness sketch.
  Handle initializer replacement during allocation and coordinated default
  method argument rules. See the current
  [constructor limitations](native-subtype-storage.md#constructor-limitations).

Exception allocation and native initializers are already implemented; their old
queue entry has been retired. These follow-ups are tracked here, but need a
readiness sketch before implementation where the owning design is unsettled.

Non-starred unpacking assignment is tracked in
[Development Priorities](development-priorities.md#non-starred-unpacking-assignment).
