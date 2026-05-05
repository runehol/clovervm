# Iteration Plans

## Goal

Define a fast `for` loop substrate for VM-internal iterable types without making
ordinary loop execution depend on Python-visible `StopIteration`.

The design center is a loop-site iteration plan:

```text
FOR_PREP:
  observe the iterable
  profile its shape or exact internal class
  select an internal iteration plan
  initialize hidden frame-local state registers

FOR_ITER:
  advance the selected plan
  place the next item in the accumulator, or jump to loop exit
```

The public Python iterator protocol remains the correctness fallback. Internal
plans are an optimization and representation choice for VM-known iterable types.

## Shape/Class Feedback

Loop sites should profile the iterable's observed shape/class. CloverVM already
uses shape as a cheap identity token elsewhere, so the recorded feedback token
may be a shape when that is the cheapest observation.

The semantic guarantee must still come from the selected plan, not from shape in
the abstract. Shape/class feedback means "this recognized internal iterable
shape/class has been observed at this loop site and maps to this VM iteration
plan." It does not mean arbitrary Python objects with compatible-looking shapes
may opt into the plan.

In the interpreter, this feedback is not expected to remove the `FOR_ITER`
switch over frame-local plan kinds. `FOR_PREP` can select a plan and initialize
state, but `FOR_ITER` still has to dispatch on the active plan tag stored in the
current frame. The feedback becomes more valuable for a future generic type
feedback system and JIT specialization, where compiled code can guard the
observed shape/class and inline the concrete plan body.

This does not rule out inline caches inside the generic Python fallback. If a
loop falls back to ordinary Python iteration, the `__iter__` lookup/call in
`FOR_PREP` and the repeated `__next__` lookup/call in `FOR_ITER` should use the
same kind of attribute and call inline caches as other dynamic method calls. The
distinction is that those ICs speed up the public protocol fallback; they do not
replace the interpreter's dispatch over internal plan kinds.

Early fast plans should be limited to exact VM-internal iterable types:

```text
range        -> RangePlan
list         -> ListPlan
tuple        -> TuplePlan
dict         -> DictKeysPlan
dict_keys    -> DictKeysPlan
dict_values  -> DictValuesPlan
dict_items   -> DictItemsPlan
generator    -> GeneratorPlan
fallback     -> PythonIteratorPlan
```

For user classes, shape may eventually participate in a guard, but only if it
also proves the relevant `__iter__` / `__next__` lookup behavior and invalidates
on class or descriptor mutation. That is not needed for the first implementation.

## Two-Stage Plan Selection

`FOR_PREP` can discover a fast plan in two different ways:

```text
direct iterable plan:
  the original iterable is a recognized exact internal iterable
  initialize a plan directly from that object

returned iterator plan:
  call iterable.__iter__()
  the returned iterator is a recognized exact internal iterator
  initialize a plan from the returned iterator object
```

The first path covers builtin containers and ranges:

```text
for x in range_obj:  RangePlan
for x in list_obj:   ListPlan
for x in tuple_obj:  TuplePlan
for x in dict_obj:   DictKeysPlan
```

The second path matters for ordinary Python objects that delegate iteration to
VM-internal iterator objects:

```python
class Bag:
    def __iter__(self):
        return iter(self.items)
```

`Bag` itself is not eligible for a direct internal plan, but if `__iter__`
returns an exact internal list iterator, tuple iterator, dict view iterator, or
generator, `FOR_PREP` may still select the corresponding returned-iterator plan.

This means the plan resolver should roughly be:

```text
if exact internal iterable has direct plan:
  initialize direct plan
else:
  iterator = cached_call(iterable.__iter__)
  if exact internal iterator has returned-iterator plan:
    initialize returned-iterator plan
  else:
    initialize PythonIteratorPlan(iterator)
```

Direct plans must not apply to subclasses or arbitrary user objects merely
because they resemble an internal type. Subclasses and user objects get their
Python-visible `__iter__` behavior unless a later guard proves that skipping it
is semantically valid.

## Frame-Local State

The active cursor state belongs in the activation, not in shared loop-site
feedback or metadata storage. Recursive or re-entrant calls to the same code
object must not overwrite an outer activation's loop state.

A small fixed state block is sufficient for the first tier:

```text
state[0]: plan tag / protocol discriminator, encoded as an SMI
state[1]: primary object or current value
state[2]: index, stop value, or scan position
state[3]: auxiliary state such as step, length, or version
```

Plans may leave slots unused. If a later plan genuinely needs more state, it can
either store a small state object in one slot or introduce a larger state-block
form. The default should stay compact because `for` loops are common.

All state slots are ordinary frame registers and must contain valid `Value`s.
The plan tag must be an SMI, not a raw enum byte or pointer. Indices, lengths,
and versions should also be SMIs when they live in the state block. Heap objects
must be stored as normal object `Value`s so the scanner can see them.

Raw pointers, side-table indices that are not valid `Value`s, cache references,
or other metadata must not be stored directly in the state block. Such metadata
belongs in bytecode operands, `CodeObject` side arrays, type-feedback storage, or
future JIT metadata.

## Plan State

### RangePlan

Used for exact internal `range` iteration.

```text
state[0]: RangePlan tag
state[1]: current
state[2]: stop
state[3]: step
```

`FOR_ITER` compares `current` against `stop`, writes `current` to the
accumulator, then advances by `step`. Exhaustion is an internal branch to the
loop exit, not a pending Python exception.

### ListPlan

Used for exact internal `list` objects.

```text
state[0]: ListPlan tag
state[1]: list object
state[2]: current index
state[3]: optional mutation/version snapshot
```

Do not cache length unless CloverVM deliberately chooses snapshot semantics.
Python list iteration can observe appended elements, so the simple compatible
plan checks `index < list.size()` on each step.

### TuplePlan

Used for exact internal `tuple` objects.

```text
state[0]: TuplePlan tag
state[1]: tuple object
state[2]: current index
state[3]: cached length
```

Tuple length is immutable, so caching length is valid.

### DictKeysPlan

Used for exact internal `dict` objects and `dict_keys` view objects.

```text
state[0]: DictKeysPlan tag
state[1]: dict object
state[2]: entry scan index
state[3]: structural version snapshot
```

The advance operation scans to the next live entry and yields its key. The dict
must expose a structural iteration version, or equivalent mutation state, so the
plan can raise the correct Python error when iteration is invalidated by a size
or structural mutation.

### DictValuesPlan

Used for exact internal `dict_values` view objects.

```text
state[0]: DictValuesPlan tag
state[1]: dict object
state[2]: entry scan index
state[3]: structural version snapshot
```

The scan is the same as `DictKeysPlan`, but yields values.

### DictItemsPlan

Used for exact internal `dict_items` view objects.

```text
state[0]: DictItemsPlan tag
state[1]: dict object
state[2]: entry scan index
state[3]: structural version snapshot
```

The scan yields a freshly allocated two-tuple `(key, value)`. A later specialized
unpacking path for `for k, v in d.items()` may avoid that tuple allocation, but
that is separate from the base iteration plan.

Tuple allocation is likely to matter for common `dict.items()` loops. The base
plan should preserve Python semantics by yielding tuples, but codegen should
eventually consider a specialized unpacking form for direct targets such as
`for k, v in d.items():` so the key and value can be assigned without allocating
the intermediate tuple.

### GeneratorPlan

Provisional plan for exact internal generator objects.

```text
state[0]: GeneratorPlan tag
state[1]: generator object
state[2]: optional resume/cache state
state[3]: unused or cached protocol data
```

Most generator state should live in the generator object. Unlike container
plans, generator advancement may resume managed Python execution. Generator
completion may use the compact stop-returning convention described in
[exception-transport-and-protocols.md](exception-transport-and-protocols.md).

This plan is intentionally provisional. Generators have re-entry checks, frame
lifetime rules, pending exception interactions, and later `send` / `throw` /
`close` behavior. It may turn out that the best interpreter implementation is
the ordinary Python iterator protocol with good `__next__` ICs, while the JIT
handles generator-specific specialization later.

### PythonIteratorPlan

Used for the generic Python iterator protocol fallback.

```text
state[0]: PythonIteratorPlan tag
state[1]: iterator object returned by __iter__
state[2]: optional __next__ lookup/call cache token
state[3]: unused or additional protocol-cache support
```

This plan preserves ordinary Python behavior:

```text
iterator = iterable.__iter__()
repeat:
  item = iterator.__next__()
  StopIteration exits the loop
  any other exception propagates normally
```

`StopIteration` may be represented compactly while crossing a VM protocol
boundary, but generic Python protocol completion is still semantically an
ordinary Python `StopIteration`.

The fallback should still be optimized. `FOR_PREP` should use an IC for
`iterable.__iter__()`, and `FOR_ITER` should use an IC for
`iterator.__next__()`. In hot loops over arbitrary Python iterator objects,
those ICs are likely to matter more for the interpreter than loop-site
shape/class feedback, because they avoid repeating generic attribute lookup,
method binding, call-cache checks, and thunk selection on every iteration.

## Dict Views

`dict.keys()`, `dict.values()`, and `dict.items()` should return distinct
internal view objects. Those view objects are exactly the values the loop-site
profiler can observe:

```text
for k in d:          iterable shape = dict
for k in d.keys():   iterable shape = dict_keys
for v in d.values(): iterable shape = dict_values
for k, v in d.items(): iterable shape = dict_items
```

Each view stores the underlying dict. `FOR_PREP` extracts that dict into the
state block and selects the appropriate dict plan.

## Relationship To StopIteration

Fast internal plans should not manufacture `StopIteration` for ordinary
exhaustion. For ranges and containers, exhaustion is just a branch to the loop
exit.

`StopIteration` remains important at protocol boundaries:

- generic `__next__` fallback
- generator completion
- user-visible `next(iterator)`
- adapters that convert VM-internal completion into public Python exceptions

This keeps exception transport as the semantic bridge, while iteration plans are
the primary performance mechanism for hot `for` loops.

## Lowering Direction

The current direct `range(...)` frontend fast path can eventually be replaced by
the general plan machinery. Rather than recognizing only producer expressions,
the loop should specialize on the iterable value it actually receives.

One possible bytecode shape is:

```text
FOR_PREP iterable_reg, state_base, fallback
FOR_ITER state_base, exit
```

`FOR_PREP` initializes `state_base..state_base+3`. `FOR_ITER` can begin as a
switch on the frame-local plan tag. Later JIT code can use generic type feedback
from the loop site to guard the profiled shape/class and inline a concrete plan.

The generic fallback remains required for correctness and for arbitrary Python
objects.

## Python Fallback Lowering

There are two plausible bytecode shapes for mixing fast internal plans with the
ordinary Python iterator protocol.

One option is to emit two loop bodies:

```text
fast loop:
  FOR_PREP_FAST
  FOR_ITER_FAST
  body
  jump fast loop

python-protocol loop:
  cached __iter__
  cached __next__
  body
  jump python-protocol loop
```

That makes each loop body simpler, but duplicates loop control-flow structure
and may complicate `break`, `continue`, `else`, and exception-table ranges.

The preferred lowering shape keeps Python protocol calls explicit while letting
the plan paths branch around them:

```text
# iterable expression leaves value in accumulator
Star state[1]

ForPrepIterablePlan state_base, loop_start
Ldar state[1]
Star a0
CallMethodAttr a0, "__iter__", iter_read_ic, iter_call_ic, 0
Star state[1]
ForPrepIteratorPlan state_base

loop_start:
ForIterPlan state_base, exit, body
Ldar state[1]
Star a0
CallMethodAttr a0, "__next__", next_read_ic, next_call_ic, 0

body:
  # accumulator contains yielded item
  ...
  Jump loop_start

exit:
```

`ForPrepIterablePlan` tries to recognize the original iterable. On success
it initializes a direct internal plan and jumps to `loop_start`, avoiding
`__iter__` entirely. On miss it falls through to the explicit cached `__iter__`
call.

`ForPrepIteratorPlan` runs after `__iter__` has returned. It tries to
recognize the returned iterator. On success it initializes a returned-iterator
plan; on miss it stores `PythonIteratorPlan` with the returned iterator in
`state[1]`. It does not need a success target because both outcomes continue to
`loop_start`.

`ForIterPlan` advances internal plans directly. If an internal plan yields,
it writes the item to the accumulator and jumps to `body`. If it exhausts, it
jumps to `exit`. For `PythonIteratorPlan`, it falls through to the explicit
cached `__next__` call.

This shape gives type feedback at both semantic decision points:

- `ForPrepIterablePlan` records whether the original iterable usually has a
  direct internal plan. The JIT needs this feedback to know whether it can skip
  the `__iter__` call.
- `ForPrepIteratorPlan` records whether the result of `__iter__` usually
  has a returned-iterator plan. The JIT needs this feedback for objects that
  delegate iteration to internal iterator types.
- `ForIterPlan` records the active plan observed at the loop backedge. The
  JIT can use that to inline the concrete iteration step or leave the explicit
  `__next__` path in place.

The two prep opcodes are not accidental duplication: they correspond to two
different observations, before and after the Python-visible `__iter__` call.

The main advantage of this shape is that Python protocol method and function
calls remain explicit bytecode. `__iter__` and `__next__` use ordinary
`CallMethodAttr` instructions, so they get the existing attribute-read and
function-call inline caches, the existing call-window encoding, ordinary
exception-table coverage, and future JIT call handling without a separate cache
system hidden inside the `for` opcodes.

The explicit `__next__` call must be covered by a synthetic exception-table
range. Its handler should route public `StopIteration` to loop exhaustion, clear
the active exception, and then jump to the shared loop exit/`else` target. Other
exceptions must be reraised through the ordinary managed exception path:

```text
ForIterPlan state_base, exit, body
Ldar state[1]
Star a0
protected_start:
CallMethodAttr a0, "__next__", next_read_ic, next_call_ic, 0
protected_end:

body:
  ...
  Jump loop_start

stop_iteration_handler:
  LdaConstant StopIteration
  ActiveExceptionIsInstance
  JumpIfFalse propagate_exception
  ClearActiveException
  Jump exit

propagate_exception:
  ReraiseActiveException
```

This keeps Python-protocol loop exhaustion semantically ordinary: the
`__next__` call raises public `StopIteration`, the synthetic loop handler
consumes it, and the shared exit path runs with no active exception.
