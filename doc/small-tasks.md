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

### Fill narrow builtin constructor gaps

Add one builtin constructor at a time where the allocation/conversion policy is
already settled and can be expressed as that type's intrinsic `__new__`. Keep
each constructor as its own change. Candidate slices include:

- `float()` and `float(x)` for the numeric input forms already supported by the
  runtime;
- `list()` for the empty case before iterable conversion is attempted;
- `tuple()` for the empty case before iterable conversion is attempted.

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

### Construct builtin objects without per-type `__new__`

Add a standard allocation path for builtin classes whose construction policy is
already settled, without requiring every such class to publish a type-specific
`__new__`. Use builtin exceptions as the first slice so calls such as
`ValueError()` and `ValueError("message")` construct objects that can
subsequently be raised.

Before implementation, pin down the eligibility guard and internal allocation
contract: which builtin classes may use standard allocation, how constructor
arguments are initialized, and how an inherited or overridden `__new__` takes
precedence. `NativeLayoutId` describes how to interpret an object that already
exists; it is not by itself an allocation recipe. Do not add a fallback that
guesses how to allocate arbitrary builtin layouts.

The exception slice is done when interpreter tests cover zero and one argument,
raising a constructed exception, its Python-visible arguments/message state,
invalid argument counts, exception subclasses, and precedence for an explicit
`__new__`. If the eligibility rule needs new public metadata or changes the
general class-call protocol, settle that design before implementation.
Native-subtype storage, extension-type allocation, and copying-GC extent
metadata remain related design questions recorded in
[Native Subtype Storage](native-subtype-storage.md); this task must not settle
them accidentally.

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
