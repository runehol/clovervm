# Call Argument Temporaries And Lazy Tracebacks

## Goal

This note ties together two constraints on call lowering:

- direct method-call lowering must preserve Python's method lookup ordering
- lazy traceback preservation should copy live frame state, not dead call
  scratch space

Call arguments live in ordinary temporary registers. A call reserves a
call-frame-aligned temporary span, evaluates arguments while that span remains
live, and enters the callee using that span as the argument window. Nested calls
reserve their own spans above any already-live outer call span, so they cannot
overwrite prepared outer arguments.

This model keeps the frame's relevant state close to the compiler's lifetime
model:

```text
locals + live temporaries + live call-argument spans
```

## Method Lookup Ordering

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

The preserving shape is:

```text
evaluate receiver
load method in call context, producing callable + maybe-self
evaluate explicit arguments
call callable with maybe-self prepended if present
```

Fused direct method-call opcodes that resolve the attribute at call execution
time do not preserve this ordering if arguments have already been evaluated.

## Call Argument Spans

A call argument span is an ordinary temporary-register reservation with
call-frame alignment. Since a `Value` is 8 bytes, the span's first semantic
register must place the callee frame pointer on a 16-byte boundary.

For each fixed call site:

```text
reserve a call-frame-aligned temporary span
evaluate the callable/receiver and arguments while the span remains live
emit a call instruction that consumes the span
release the span when the call has committed/returned
```

The span's semantic size is the number of argument registers the call opcode
uses. Alignment padding is an allocator detail. Zero-argument calls still
reserve a one-register semantic anchor, but the emitted argument count remains
zero; the anchor gives call adaptation a valid aligned insertion point for any
extra arguments it must synthesize.

At the call opcode, the call argument span must be the topmost live temporary
reservation. Call adaptation may use registers beyond the semantic argument
span, so no unrelated temporary may sit above it.

Keyword calls have two live regions:

```text
keyword value span
positional/call-frame argument span
```

The keyword value span is allocated before the positional argument span. Both
regions remain live across all argument evaluation. This ordering keeps keyword
values out of the area that call adaptation may extend above the positional
argument span.

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
arg_span = reserve_call_frame_temps(n_user_args + 1)

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

The move is required for CloverVM's frame-entry convention. Starting the
argument vector at `arg_span[1]` would put the callee frame on the wrong
16-byte boundary, so the no-self path copies the explicit arguments down before
entering the callee.

The callee does not need method-specific bytecode. It receives an ordinary
contiguous positional argument vector:

```text
obj.method(1, 2)  -> f(obj, 1, 2)
f(obj, 1, 2)      -> f(obj, 1, 2)
```

Only the caller-side call setup differs.

Fixed keyword method calls use the same split. Positional arguments are
evaluated into the reserved method-call span. Keyword values are evaluated into
the keyword value span allocated below it. `CallPreparedMethodKeyword` performs
the same maybe-self normalization before entering the keyword-call path.

## Unpack Calls

Calls that unpack `*args` or `**kwargs` do not need a parallel set of
prepared-method opcodes. Their argument layout is already dynamic:

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

evaluate args_iterable / kwargs_mapping

CallUnpack callable_tmp, method_self_tmp, args_iterable
CallUnpackKeyword callable_tmp, method_self_tmp, args_iterable, kwargs_mapping
```

For fixed prepared-method calls, the maybe-self value lives directly in the
reserved leading slot of the argument span. For star calls there is no fixed
argument span yet, so `LoadMethodAttr` writes the maybe-self value to an
ordinary temporary. If that temporary is present, unpack-call expansion writes it
as positional argument 0. If it is `not_present`, expansion starts with the
explicit expanded arguments. Because the final aligned argument span is built
dynamically, there is no fixed leading-slot move to optimize.

This keeps the intended call opcode surface to:

```text
CallPositional
CallKeyword
CallPreparedMethodPositional
CallPreparedMethodKeyword
CallUnpack
CallUnpackKeyword
```

The fixed positional/keyword paths stay specialized for their common layouts,
while the irregular star paths stay generic and self-aware.

## Regular Calls

Regular calls start explicit arguments at slot 0:

```text
callable_tmp = evaluate callable
arg_span = reserve_call_frame_temps(max(n_args, 1))

for each argument i:
    evaluate argument into arg_span[i]

Call callable_tmp, arg_span, n_args
release arg_span and callable_tmp
```

A CPython-style always-present `self_or_null` slot should not be added to every
regular call without strong evidence. A leading maybe-self slot would make the
common non-bound regular call start its real arguments at slot 1. Because slot 1
is only 8-byte aligned, the VM would then either have to copy arguments down for
ordinary calls or allow misaligned managed frame entry.

One runtime case still needs to insert a receiver for ordinary call syntax:

```python
c = obj.method
c(1, 2)
```

Here codegen sees a regular call, but the callable may be a Python-visible bound
method object. The preferred tradeoff is:

- direct method-call syntax reserves a leading maybe-self slot, because binding
  is common there
- regular calls start explicit arguments at slot 0
- calling a bound method object through a variable uses a slower adaptation path
  that builds an aligned argument span with the receiver inserted

## Callee `**kwargs`

This note only covers callee-side `**kwargs` collection for existing explicit
keyword calls. Caller-side `**mapping` unpack remains separate work.

The call opcodes should not each learn how to build a `**kwargs` dictionary.
Instead, keyword collection belongs in the shared callee binding/adaptation
layer. Each call form still produces the same logical inputs:

```text
positional argument span
optional keyword names tuple
keyword value span
```

For callees with a `**kwargs` parameter, positional-only calls initialize that
parameter slot with an empty dict. Keyword calls use the existing keyword remap
table with one sentinel entry for keywords that should be collected into that
dict.

If the remap table currently maps each call-site keyword to a callee parameter
slot, reserve one impossible slot value:

```cpp
static constexpr int16_t KeywordRemapToKwargsDict = -1;
```

or, if the table remains unsigned:

```cpp
static constexpr uint16_t KeywordRemapToKwargsDict = UINT16_MAX;
```

The call plan/cache should also carry a flag such as:

```cpp
bool has_kwargs_dict_targets;
```

That lets the common keyword-call fast path stay tight when all keywords bind
formal parameters:

```text
if !has_kwargs_dict_targets:
    copy every keyword value to its remapped formal slot
else:
    sentinel remap entries insert into the kwargs dict
```

Cache setup classifies each explicit keyword name in this order:

```text
name maps keyword-bindable formal:
    remap[i] = formal slot, unless that formal was already filled

name maps positional-only formal and callee has **kwargs:
    remap[i] = KeywordRemapToKwargsDict
    has_kwargs_dict_targets = true

name maps positional-only formal and callee has no **kwargs:
    TypeError

name is unknown and callee has **kwargs:
    remap[i] = KeywordRemapToKwargsDict
    has_kwargs_dict_targets = true

name is unknown and callee has no **kwargs:
    TypeError
```

This preserves Python's duplicate/multiple-value behavior:

```python
def f(a, **kwargs):
    ...

f(1, a=2)   # TypeError, not kwargs={"a": 2}
```

Positional-only names are different. Python allows them to appear as keyword
names when the callee has `**kwargs`, because they cannot bind the
positional-only parameter:

```python
def f(a, /, **kwargs):
    return kwargs

assert f(1, a=2) == {"a": 2}
```

For static explicit `CallKeyword`, parser/codegen already reject repeated
keyword names at the call site, so dynamic duplicate-name handling can stay out
of this initial callee-side `**kwargs` work. Runtime duplicate handling belongs
with future caller-side `**mapping` unpack support.

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
- A call instruction only consumes a call-argument span that is the topmost live
  temporary reservation.
- The span size reported to call emitters is semantic size; allocator padding is
  not visible to bytecode.
- Zero-argument calls reserve a one-register semantic anchor while still
  emitting argument count zero.
- Keyword value spans are allocated before positional call-argument spans and
  stay live across all argument evaluation.
- Nested calls allocate separate spans and cannot overwrite an outer live span.
- Call instructions identify the argument span they consume.
- After a call commits, the caller's argument span is dead unless the call
  failed before frame entry in a way that still needs those values.
- Traceback preservation copies live frame state, not maximum frame capacity.
- Lazy traceback preservation must not treat uninitialized alignment padding or
  uninitialized call-span slots as Python-visible frame state.

## Open Questions

- What live extent is required for frame introspection beyond `f_locals`?
  Copying locals and live temporaries is more complete; copying only locals is
  cheaper but may constrain future debugging features.
- Should `LoadMethodAttr` plus `CallPreparedMethodPositional` /
  `CallPreparedMethodKeyword` replace the fused
  `CallMethodAttrPositional` and `CallMethodAttrKeyword`, or should the fused
  opcodes remain only as later peephole/JIT shapes proven to preserve ordering?
- Which opcodes can raise before a callee frame is committed, and what argument
  span state must be preserved or cleaned up in those paths?
- How should lazy traceback preservation represent per-instruction temporary
  liveness, especially for partially initialized call spans?

## Non-Goals

- This does not require heap-allocating every active frame.
- This does not require segmented stack chunks.
- This does not require preserving call-entry argument snapshots for tracebacks.
- This does not change callee bytecode semantics for functions used as methods.
