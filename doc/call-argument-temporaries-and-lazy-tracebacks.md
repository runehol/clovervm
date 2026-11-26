# Call Argument Temporaries And Lazy Tracebacks

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial |
| Scope | Call argument staging, evaluation order, and traceback-safe temporary lifetimes |
| Owning layers | Codegen, managed frames, interpreter calls, and exception handling |
| Validated against | `ec7d0ff` (2026-05-30) |
| Supersedes | N/A |

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

`LoadMethod` saves a pair in adjacent logical registers. The callable is at
`S`, and the optional receiver is at `S+1`. Bytecode names only `S`. Frame
offsets descend, so the physical slots are `fp[encoded_S]` and
`fp[encoded_S - 1]`. Only `Value::not_present()` represents an absent
receiver. `None` is a valid receiver, and both `None` and `NotImplemented` can
be ordinary attribute results.

### Managed getter return

```text
LoadMethod        base, name, read_ic
FinishLoadMethod  S
<evaluate arguments>
CallMethodPositional S, positional_count, call_ic
```

The two encoded opcodes form one semantic load. On a direct lookup,
`LoadMethod` reads `S` from the following instruction, writes the pair, and
dispatches directly to argument evaluation. Codegen may place the base in
`S+1`; the handler reads the base before writing either output.

When lookup selects a Python getter, the interpreter enters it through the
existing managed call path used by `LoadAttr`, with `FinishLoadMethod` as the
return PC. Getter arguments use the implicit call window below the caller's
reserved temporaries. `FinishLoadMethod` reads its own `S` operand, stores the
accumulator in `S`, writes `not_present` to `S+1`, and dispatches to argument
evaluation. The accumulator is scratch across the semantic load. This keeps
recursive getters on the normal threaded managed path.

The pair is defined at successful completion. A missing attribute or a
failed getter raises at the load, before arguments run. The saved return PC
must allow exception unwinding to associate a getter failure with
`LoadMethod`. A safepoint after the getter returns and before the pair is
written publishes the continuation PC and roots the return value in the
accumulator. Resuming there commits the pair exactly once.

Sequential decoding and initial JIT fallback treat the two opcodes as one
operation. The semantic load uses the base, defines both pair slots, and
invalidates the incoming accumulator state. Standalone decoding at
`FinishLoadMethod` identifies the accumulator input and pair destination.
The initial JIT exits before the load, with the base recoverable. Any exit
after a committed load keeps both pair slots wherever they are live and
resumes forward. A snapshot at a getter return would instead need the
continuation PC and return value. See
[Bytecode Decoding](bytecode-decoding-and-blocks.md#compound-operators) and
[JIT snapshot state](jit-bytecode-state-tracking.md#complete-state-capture-and-snapshots).

### Positional and keyword calls

Codegen reserves `[callable, receiver-or-not_present, positional args…]` in one
`MethodCallFrame` span, with any padding before `S` so the outgoing argument
window starts at aligned `S+1`. Explicit positional arguments begin at `S+2`.
With zero explicit arguments, `S+1` still provides the aligned anchor. For keyword calls,
codegen reserves the separate keyword value span before this reservation to
keep it clear of outgoing argument adaptation.

```text
CallMethodPositional S, positional_count, call_ic
CallMethodKeyword    S, positional_count, keyword_values, keyword_count, keyword_names, keyword_ic
```

The call uses the selected callable after argument evaluation. A present
receiver stays in the leading slot. If neither lookup nor callable resolution
supplies a receiver, explicit positional values move into the aligned span
starting at `S+1`. The callable stays outside the outgoing parameter window.
All three method call forms consume the pair, use the existing call adapters,
and produce their result in the accumulator.

### Expanded calls

```text
CallMethodExpanded S, containers, outgoing_anchor, expanded_ic
```

The pair and the adjacent tuple/dict transport slots survive all argument
evaluation and expansion. They sit outside the separate aligned outgoing
window. Codegen passes the saved callable as the context for keyword
construction errors. The existing expansion order applies, including lone
star conversion after keyword construction. The expanded binder consumes
the tuple through its existing optional-prefix view, so passing an implicit
receiver requires no new tuple. A callee's `*args` collector can still require
a tuple when its suffix includes the receiver.

Plain calls continue to use `CallPositional`, `CallKeyword`, and
`CallExpanded`. See [Function Call Adaptation](function-call-adaptation.md#expanded-call-transport)
for argument construction, binding, and collector rules.

### Lifetimes and verification

Both pair slots stay live through argument evaluation, including nested
calls, expansion callbacks, and safepoints. Reserving the span leaves each
argument slot undefined until evaluation stores its value. Nested calls have
their own outgoing windows. After call commit the caller's outgoing span is
dead. Check the signed register-operand limits with the additional slot and
alignment padding before choosing an encoding or layout change.

The regression suite checks lookup timing across all three call forms,
including argument-side method replacement, missing attributes, getter
failure and reentry, and deferred callability checks. Hand-built bytecode
tests cover getter return, safepoints, pair alignment, and call adaptation.

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

This section covers callee-side `**kwargs` collection for explicit keyword
calls. [Expanded-call transport](function-call-adaptation.md#expanded-call-transport)
covers caller-side `**mapping` unpack and duplicate detection.

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

For static explicit `CallKeyword`, parser and codegen reject repeated keyword
names at the call site. Expanded calls detect dynamic duplicate names during
keyword construction.

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
- Which opcodes can raise before a callee frame is committed, and what argument
  span state must be preserved or cleaned up in those paths?
- How should lazy traceback preservation represent per-instruction temporary
  liveness, especially for partially initialized call spans?

## Non-Goals

- This does not require heap-allocating every active frame.
- This does not require segmented stack chunks.
- This does not require preserving call-entry argument snapshots for tracebacks.
- This does not change callee bytecode semantics for functions used as methods.
