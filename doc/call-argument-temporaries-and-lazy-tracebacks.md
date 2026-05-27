# Call Argument Temporaries And Lazy Tracebacks

## Goal

This note records a proposed direction for two related problems:

- direct method-call lowering must preserve Python's method lookup ordering
- lazy traceback support should not force every frame to carry a permanent
  outgoing-call area that later has to be copied as dead state

The core idea is to treat call arguments as ordinary temporary ranges reserved
by codegen, rather than as a separate frame-global outgoing argument area.
When an exception unwinds past a frame, traceback preservation can then copy the
actual live frame extent with one bulk copy, without also copying a hole of dead
outgoing argument slots.

This is a design note, not a description of the current implementation. The
current calling convention is documented in
[function-calling-convention.md](function-calling-convention.md).

## Problem: Method Lookup Ordering

Python evaluates the receiver and resolves the method attribute before
evaluating call arguments. That ordering is visible if an argument expression
mutates the receiver's class:

```python
class C:
    def method(self, value=None):
        return 1

obj = C()

def mutate():
    def replacement(self, value=None):
        return 2

    C.method = replacement
    return 0

assert obj.method(mutate()) == 1
```

The method selected by `obj.method` must be the method visible before
`mutate()` runs. Argument side effects may mutate the object or class, but they
must not affect which method target this call uses.

CPython preserves this while still optimizing method calls by splitting the
operation:

```text
evaluate receiver
load method in call context, producing callable + maybe-self
evaluate explicit arguments
call callable with maybe-self prepended if present
```

CloverVM's current fused direct method-call opcodes resolve the attribute at
call execution time, after arguments have already been evaluated. That makes
argument mutation semantically visible in the method lookup.

## Problem: Outgoing Argument Slots

CloverVM currently has a distinct outgoing argument area (`a0`, `a1`, ...). That
area is useful for native-call alignment and frame entry, but it creates two
pressures:

- prepared arguments can be clobbered by nested calls during later argument
  evaluation unless the area is specially protected
- a permanent outgoing area is dead state after a call commits, but it still
  expands the physical frame footprint that lazy traceback preservation might
  need to copy

Lazy tracebacks do not need call-entry argument snapshots. Python tracebacks
expose frames, locations, source text, and current/preserved locals. Once a
callee frame has been entered, parameters live in the callee's local slots. If
that frame later unwinds into a traceback, preserving the frame is the relevant
operation, not preserving the caller's old outgoing argument area.

## Proposed Direction

Codegen reserves call arguments from the ordinary temporary area.

For each call site:

```text
reserve a 16-byte-aligned temporary span for the call arguments
evaluate the callable/receiver and arguments while that span remains live
enter the callee using that span as the argument window
release the span when the call has committed/returned
```

Nested calls allocate their own temporary spans after the currently live spans.
They cannot overwrite arguments already prepared for an outer call.

The alignment requirement is part of the call protocol, not just an allocator
preference. Since a `Value` is 8 bytes, a valid call argument span must start on
an even slot boundary so the callee frame remains 16-byte aligned for native ABI
interoperability.

This keeps the frame's live extent honest:

```text
locals + live temporaries + live call-argument spans
```

instead of:

```text
locals + temporaries + permanent outgoing-call area
```

At exception unwind time, preserving a frame for a future traceback can be a
single allocation plus one bulk copy of the live frame extent. The copy should
not include a maximum outgoing-call area unless that area is actually live for
the faulting instruction.

## Direct Method Calls

Direct method calls reserve one extra leading argument slot for a possible
implicit receiver:

```text
arg_span[0] = reserved maybe-self slot
arg_span[1] = explicit argument 0
arg_span[2] = explicit argument 1
...
```

Lowering shape:

```text
arg_span = reserve_aligned_call_temps(n_user_args + 1)

receiver_tmp = evaluate receiver
callable_tmp = LoadMethodAttr(receiver_tmp, name, arg_span[0])

for each explicit positional argument i:
    evaluate argument into arg_span[i + 1]

CallPreparedMethodPositional callable_tmp, arg_span, n_user_args
release arg_span and call temps
```

`LoadMethodAttr` is a call-context attribute load. It does not produce a
Python-visible bound method object. It produces:

```text
callable_tmp = function/callable to invoke
arg_span[0]  = receiver if descriptor binding produced an implicit self,
               otherwise not_present
```

Plain `LoadAttr` remains Python-visible attribute access and must still produce
a bound method object when Python semantics require one.

`CallPreparedMethodPositional` normalizes the temporary argument span before
entering the callee:

```text
if arg_span[0] is present:
    call callable_tmp with arg_span[0..n_user_args]
else:
    move arg_span[1..n_user_args] down to arg_span[0..n_user_args-1]
    call callable_tmp with arg_span[0..n_user_args-1]
```

The no-self case pays the move. That is acceptable because ordinary class
function method calls bind `self`; no-self method-call syntax is the less common
case.

The move is required for CloverVM's frame-entry convention. CPython can model
the no-self case as starting the argument vector one stack slot later. CloverVM
cannot do that directly because call argument spans must remain 16-byte aligned;
with 8-byte `Value` slots, starting at `arg_span[1]` would misalign the callee
frame. The no-self path therefore has to copy the explicit arguments down by one
slot before entering the callee.

The callee does not need method-specific bytecode. It receives an ordinary
contiguous positional argument vector:

```text
obj.method(1, 2)  -> f(obj, 1, 2)
f(obj, 1, 2)      -> f(obj, 1, 2)
```

Only the caller-side call setup differs.

Fixed keyword method calls use the same split. Positional arguments are
evaluated into the reserved method-call span, keyword values are evaluated into
the keyword value span, and `CallPreparedMethodKeyword` performs the same
maybe-self normalization before entering the keyword-call path.

## Star Calls

Star-argument calls do not need a parallel set of prepared-method opcodes.
Their argument layout is already dynamic:

```python
obj.method(*args)
obj.method(1, *args, **kwargs)
```

The runtime must expand iterables, merge keyword mappings, compute the final
argument count, and materialize a final call span. A prepared method receiver is
just another optional prefix during that expansion.

Method-call lowering for star calls can therefore be:

```text
receiver_tmp = evaluate receiver
method_self_tmp = reserve temporary
callable_tmp = LoadMethodAttr(receiver_tmp, name, method_self_tmp)

evaluate starargs / starkwargs

CallStarArgs callable_tmp, method_self_tmp, starargs
CallStarArgsStarKwargs callable_tmp, method_self_tmp, starargs, starkwargs
```

For fixed prepared-method calls, the maybe-self value lives directly in the
reserved leading slot of the argument span. For star calls there is no fixed
argument span yet, so `LoadMethodAttr` writes the maybe-self value to an
ordinary temporary. If that temporary is present, star-call expansion writes it
as positional argument 0. If it is `not_present`, expansion starts with the
explicit expanded arguments. Because the final aligned argument span is built
dynamically, there is no fixed leading-slot move to optimize.

This keeps the intended call opcode surface to:

```text
CallPositional
CallKeyword
CallPreparedMethodPositional
CallPreparedMethodKeyword
CallStarArgs
CallStarArgsStarKwargs
```

The fixed positional/keyword paths stay specialized for their common layouts,
while the irregular star paths stay generic and self-aware.

## Regular Calls

Regular calls reserve exactly the explicit argument count:

```text
callable_tmp = evaluate callable
arg_span = reserve_aligned_call_temps(n_args)

for each argument i:
    evaluate argument into arg_span[i]

Call callable_tmp, arg_span, n_args
release arg_span and callable_tmp
```

If a future unified call protocol wants a CPython-style always-present
`self_or_null` slot, regular calls can reserve and fill a leading sentinel slot.
That should be justified by implementation simplicity or performance evidence;
with CloverVM's 16-byte alignment requirement, starting the call from the second
slot is not a valid substitute for normal frame entry.

One runtime case still needs to insert a receiver for ordinary call syntax:

```python
c = obj.method
c(1, 2)
```

Here codegen sees a regular call, but the callable may be a Python-visible bound
method object. CPython handles this by giving every call a `self_or_null` stack
slot; regular calls push `NULL`, and bound-method expansion can replace that
slot with the method's receiver.

CloverVM should not adopt that exact layout for every regular call unless there
is strong evidence for it. A leading maybe-self slot would make the common
non-bound regular call start its real arguments at slot 1. Because slot 1 is
only 8-byte aligned, the VM would then either have to copy arguments down for
ordinary calls or allow misaligned managed frame entry.

The preferred tradeoff is:

- direct method-call syntax reserves a leading maybe-self slot, because binding
  is common there
- regular calls start explicit arguments at slot 0
- calling a bound method object through a variable uses a slower adaptation path
  that builds an aligned argument span with the receiver inserted

## Alignment Invariant

Every CloverVM call argument span and callee frame entry must start 16-byte
aligned.

The VM should not violate the managed-frame alignment convention and repair only
at native-entry boundaries. That would make frame alignment depend on source
call shape, such as no-self method calls or bound-method-object expansion. The
result would be a low-level, data-dependent invariant leak: native thunks, JIT
entry, stack scanning, frame headers, and helper code would all need to tolerate
both aligned and misaligned managed frames.

The local copy in uncommon call-shape adjustment paths is preferable to global
alignment ambiguity.

## Traceback Preservation

When an exception unwinds through Python frames that may be exposed through a
traceback, the VM should preserve the unwound frame span with one bulk copy
where possible:

```text
identify the contiguous stack span from the raise frame through the last
  unwound frame before the catching frame
allocate one preserved-traceback-span blob sized to that live stack span
memcpy the frame headers, locals, and live temporary slots into the blob
record per-frame code objects and saved instruction/source locations
```

If execution stays entirely in Python while unwinding, the common case is one
copy from the raising frame up to, but not including, the catching frame. The
preserved span then contains all frames that have been logically popped but may
later be exposed through `e.__traceback__`.

This is distinct from materializing Python-visible traceback and frame objects.

- preserve: retain enough frame state to materialize later
- materialize: allocate Python-visible traceback/frame objects

The catching frame does not need to be copied when entering its handler. It
continues to run, and its locals remain live. Mutations performed by the handler
should be visible through that frame's traceback object if the traceback
references the catching frame.

Frames that have been unwound past are different: their stack storage is no
longer live execution state. If the traceback may later expose them, their
locals must have been preserved before that storage is reused.

## GC And Lifetime

A preserved traceback span cannot be an untracked raw `memcpy` of `Value` slots
unless those values are otherwise retained.

The preferred shape is for preserved spans to be scanned GC roots/objects:

```text
PreservedTracebackSpan
  per-frame code object / metadata refs
  per-frame saved pc/source location
  copied Value slots for each preserved frame's locals and live temps
```

The scanner marks all copied `Value` slots that are part of the preserved live
extent. This avoids per-slot retain/release work on unwind while keeping copied
objects alive after the original frame slots are popped or reused.

If the current memory manager cannot naturally scan such blobs, the alternative
is to retain each copied `Value`, but that loses much of the benefit of the
single bulk-copy design.

## Compiler/Runtime Invariants

This plan depends on a few explicit invariants:

- Call-argument temporary spans are ABI-aligned.
- Reserved call-argument spans remain live across evaluation of all later
  arguments.
- Nested calls allocate separate spans and cannot overwrite an outer live span.
- Call instructions identify the argument span they consume.
- After a call commits, the caller's argument span is dead unless the call
  failed before frame entry in a way that still needs those values.
- Each safepoint/exception point can report the frame's live extent for scanning
  and preservation.
- Traceback preservation copies live frame state, not maximum frame capacity.

## Open Questions

- How should the temporary allocator express an aligned range reservation?
- Should call argument spans be released lexically by RAII in codegen, or should
  bytecode carry explicit stack-top/lifetime metadata?
- What live extent is required for frame introspection beyond `f_locals`?
  Copying locals and live temporaries is more complete; copying only locals is
  cheaper but may constrain future debugging features.
- How should keyword-call remapping interact with temporary argument spans?
- Should `LoadMethodAttr` plus `CallPreparedMethodPositional` /
  `CallPreparedMethodKeyword` replace the existing fused
  `CallMethodAttrPositional` and `CallMethodAttrKeyword`, or should the old
  opcodes remain only as later peephole/JIT shapes proven to preserve ordering?
- Which opcodes can raise before a callee frame is committed, and what argument
  span state must be preserved or cleaned up in those paths?

## Non-Goals

- This does not require heap-allocating every active frame.
- This does not require segmented stack chunks.
- This does not require preserving call-entry argument snapshots for tracebacks.
- This does not change callee bytecode semantics for functions used as methods.
