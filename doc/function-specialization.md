# Function Specialization

| Field | Value |
|---|---|
| Document type | Investigation |
| Status | Speculative |
| Implementation | Not started |
| Scope | Guarded alternative implementations for ordinary function calls and protocol dispatch |
| Owning layers | Runtime call resolution owns applicability; call-site caches own observed specialization evidence; the JIT owns guarded lowering; runtime builtin implementations own specialized native targets |
| Validated against | N/A |
| Supersedes | N/A |

This document records a possible generalization of clovervm's trusted-handler
mechanism. It is not an accepted implementation direction. Its purpose is to
preserve a useful architectural idea while ordinary calls, trusted-handler
metadata, and JIT call lowering are still evolving.

## Motivation

A registered native function currently exposes complete Python behavior through
a small synthetic `Function` and `CodeObject`. Its bytecode calls a C++ target
and returns. Type checks, shape discrimination, and fallback behavior performed
inside that C++ target are opaque to the JIT.

Trusted operator handlers have a different and potentially more useful shape.
The runtime resolves operand types and protocol state before selecting a native
handler. A call site can therefore retain the proof that a particular handler
is valid, and the JIT can turn that proof into explicit guards. Important
handlers can additionally carry a semantic identity such as integer addition
or integer-indexed tuple access.

The speculative direction is to make that capability available to ordinary
function calls. A function would retain one complete implementation and could
also offer guarded specializations:

```text
Function
`-- CodeObject
    |-- complete implementation
    `-- optional specialization resolver
          canonical call evidence
              -> FunctionSpecialization
```

This is more than handler metadata. It generalizes dispatch so a call site may
invoke a selected specialization instead of entering the complete function.

## Vocabulary

### Function specialization resolver

A `FunctionSpecializationResolver` decides whether a specialization applies to
one canonical call shape. A resolver declares a specialization prefix length
`N` and receives only the ordered `ShapeKey` values of the first `N` adapted
arguments. It does not inspect later argument shapes, argument values, object
contents, or interpreter frame layout, and it does not execute the operation.

The resolver belongs to the `CodeObject`. A `Function` continues to own
per-function call state such as defaults, but its code object owns both the
complete executable implementation and the specializations of that
implementation. Protocol dispatch must first select the concrete function and
put its arguments into function-call order. Normal versus reflected protocol
selection is therefore resolved before specialization rather than becoming an
extra resolver input.

### Function specialization

A `FunctionSpecialization` is the resolver's proof-carrying result. It identifies
the required call-site predicates and the selected target. Conceptually:

```text
FunctionSpecialization {
    required argument shape prefix: [shape0, ..., shapeN-1]
    specialized call target
}
```

Exact function identity and lookup validity belong to the enclosing call-site
plan. They establish which code object and resolver were selected; they are not
inputs to, or outputs from, the resolver itself.

Every shape in the prefix is guarded. Arguments after the prefix have no shape
guard and cannot influence specialization selection. They are still passed to
and may be used by the specialized target. For example, a specialized
`setitem(receiver, key, value)` target may declare `N = 2` and require:

```text
[receiver_shape, key_shape]
```

The target still receives and stores `value`; its applicability merely does not
depend on `value`'s shape. Restricting specialization to a prefix avoids sparse
shape masks, keeps cache matching dense, and makes the resolver API enforce
which argument shapes it may inspect. It deliberately cannot specialize on a
later argument while ignoring an earlier one.

### Specialized call target

A `SpecializedCallTarget` is the action selected by a specialization. The first
useful target is a native trusted handler. Its function pointer identifies
metadata registered during VM builtin setup:

```text
native handler pointer
    -> semantic identity
    -> declared effects
```

An unrecognized semantic identity remains callable as an opaque native target
with its declared effects. A recognized identity may lower to explicit Core IR
instead of retaining the call.

Semantic identities must describe the actual specialization, not merely the
Python protocol. For example, integer-indexed tuple access and slice-based tuple
access are different targets: the latter may allocate while the former need
not.

## Call-Site Model

Specialization resolution occurs after ordinary function adaptation has
produced the callee's canonical parameter values. Defaults have been inserted,
keyword arguments have been moved into their parameter positions, and required
variadic containers have been constructed. A function declared conceptually as
`f(*args, **kwargs)` therefore presents two arguments to its resolver: the
adapted `args` tuple and `kwargs` dictionary. The resolver sees their shape keys,
not the unadapted source argument list or their contents.

This boundary deliberately leaves adaptation work outside specialization. A
later optimization may remove avoidable tuple or dictionary construction, but
it must not change what argument environment the resolver describes.

A specialized ordinary call would follow this logical flow:

```text
guard the callable lookup path and its validity cell
    -> if invalid, redo lookup and rebuild the call plan
identify the exact Function and its cached adaptation plan
    -> adapt positional, keyword, default, *args, and **kwargs state once
ask its CodeObject resolver about the canonical argument shape prefix
    -> cache the selected specialization and required prefix shapes
    -> invoke the specialized target with all canonical arguments
```

All executable targets under one `Function` consume the same adapted canonical
arguments, whether the target is bytecode, a specialized native handler, or a
future target form. A specialization shape mismatch may rerun specialization
resolution or invoke the complete function with those already-adapted
arguments. It does not repeat adaptation.

A tripped lookup validity cell is different: it means lookup may now select a
different function object, with a different signature and adaptation plan. The
validity check therefore precedes adaptation. On failure, lookup and call-plan
construction restart before any arguments are adapted for the stale function.

Specialization failure must occur before the specialized target performs
effects.

A compiled monomorphic case would resemble:

```text
guard callable lookup validity and exact function identity
perform the cached function adaptation
guard the required argument shape prefix
    -> recognized semantic Core operation
    or generic specialized native call
otherwise
    -> re-resolve specialization or call the complete function with the same
       canonical arguments
```

Polymorphic call sites could eventually retain several such cases, but bounded
polymorphism is not required to make the mechanism useful.

## Motivating Example: `len`

The Python `len` function is a shared semantic dispatcher. Across the whole
program it may receive lists, tuples, dictionaries, strings, and arbitrary
objects implementing `__len__`. Feedback collected inside `len` is therefore
naturally megamorphic even when each individual caller repeatedly passes one
stable kind of value:

```python
list_count = len(items)       # usually a list here
name_size = len(name)         # usually a string here
mapping_size = len(mapping)   # usually a dict here
```

Inlining `len` into each caller could expose its internal type and dunder
dispatch to caller-specific type inference. That asks the inliner to recover a
specialization boundary that is already conceptually present, however, and it
makes successful optimization depend on inlining policy and cleanup quality.

A specialization resolver on `len`'s code object declares `N = 1` and moves
that boundary directly to the caller:

```text
len CodeObject specialization resolver
    [list_shape]   -> ListLength
    [tuple_shape]  -> TupleLength
    [dict_shape]   -> DictLength
    [string_shape] -> StringLength
    otherwise      -> complete len implementation
```

Each `len` call site can now be monomorphic even though `len` itself is
megamorphic. If JIT type inference proves the argument shape, the selected
specialization needs no dynamic shape check. If the shape is only observed, the
JIT emits one caller-side guard and retains the complete call as fallback.

The semantic identities above stand for complete specialized `len` behavior,
not merely field loads. They must preserve Python's result and exception
contracts for the accepted exact shapes. Arbitrary classes and mutable dunder
resolution remain on the complete path under this shape-key-only resolver
contract.

This pattern applies more broadly to hot shared functions whose implementation
is mostly type or protocol dispatch. Function specialization prevents
callee-wide megamorphism from obscuring caller-local type stability without
requiring those functions to be inlined first.

## Relationship to Trusted Operator Dispatch

Trusted dunder dispatch is a natural first instance of function
specialization. Special-method lookup proves which `Function` Python semantics
would select. Operand shapes and lookup validity then justify a specialized
native target.

Ordinary calls begin with a different proof: the call cache identifies the exact
`Function` directly. After call adaptation, argument shapes can justify the same
kind of specialized target. Explicit calls such as these could therefore use
the mechanism:

```python
int.__add__(left, right)
tuple.__getitem__(value, index)
str.__contains__(value, needle)
```

Operator-specific context is consumed while selecting the concrete function and
arranging its canonical arguments. The selected code object's specialization
resolver receives only those arguments' shape keys.

This differs from the current trusted-operator API. Current resolvers receive a
normal-versus-reflected selector and return handlers normalized to the
operator's semantic operand order. A generalized function-specialization API
would instead select the concrete normal or reflected function first, arrange
that function's canonical arguments, and invoke its code object's resolver.
Adopting the generalized model therefore requires an explicit migration of
trusted operator resolution; it is not only a vocabulary change.

## Handler Metadata Registry

Specialized native targets need immutable metadata, but that metadata is mainly
consumed during JIT compilation. Duplicating it into every inline cache would
increase hot interpreter state and copy JIT-only information between call
sites.

The likely representation is a registry populated explicitly while builtin
classes and methods are installed in a `VirtualMachine`. Static constructor
registration is excluded because its initialization and threading behavior are
difficult to control.

C++ handler arity is already part of the function-pointer type. A registry can
therefore preserve the typed ABI of each supported specialized target rather
than erase every target to `void (*)()`. Unary, binary, and ternary targets cover
the current trusted-handler subset; generalized function specialization may
add higher target arities as concrete uses require them. Target arity is
independent of the specialization prefix length: a target receives every
canonical argument even when only its first `N` argument shapes participate in
resolution. Registration must reject one typed handler pointer being assigned
conflicting metadata.

The metadata vocabulary remains to be designed. It is expected to include:

- a conservative may-effect description suitable for generic native calls;
- an optional, specialization-specific semantic identity;
- enough exception information to preserve pending-exception handoff.

Purity should be derived from the absence of relevant may-effects rather than
stored as an independent and potentially contradictory property.

## Semantic Ownership

The complete function remains authoritative for Python-visible behavior. A
specialization is a partial implementation valid only under resolver-proven
preconditions. It must produce the same result or pending exception as the
complete function for every call in its accepted domain.

This contract is manageable for existing builtins when the complete native
wrapper and specialized handler share an underlying operation implementation:

```text
complete wrapper = validation + dispatch + shared semantic operation
specialization  = call-site guards + shared semantic operation
```

An arbitrary Python function paired with an independently written C++
specialization presents a greater maintenance risk. The implementations can
drift while each remains locally plausible. If that capability is ever exposed,
it should initially remain an internal VM facility and require differential
tests over the specialization's accepted domain.

Generating both implementations from a semantic DSL or higher-level operation
description could reduce duplication, but that is substantially more machinery
and is not implied by this proposal.

## Correctness Boundaries

Any adopted design must preserve these boundaries:

- Exact function identity, or an equivalent versioned dependency, guards the
  selected call adaptation state. Any validity cell protecting the lookup path
  is checked before adaptation. The selected `CodeObject` owns the complete
  implementation and specialization resolver.
- Resolver inputs are limited to the shape keys of canonical post-adaptation
  arguments in its declared prefix. A specialized target does not silently
  bypass argument binding or its errors.
- Every shape in the specialization prefix remains guarded at the call site.
  Later argument shapes are unavailable to the resolver and do not influence
  specialization selection.
- A specialization mismatch reuses the existing canonical arguments when it
  re-resolves or invokes the complete function. A failed lookup-validity guard
  instead restarts lookup and chooses the new function's adaptation plan.
- A specialized target either completes the operation or propagates its
  exception. It cannot request replay through the complete function after
  committing effects.
- Recognized Core lowering preserves the same preconditions and effects as the
  native specialized target.
- Frame, traceback, tracing, and profiling observability must be addressed
  before specialization is allowed to remove a Python-visible frame.
- Mutable function code, defaults, or specialization state require explicit
  versioning or invalidation if those features become observable.

## Possible Runtime Use

The same specialization could be consumed by the interpreter as well as the
JIT. A function-call inline cache could retain function identity, argument shape
guards, and a selected specialized target, then invoke that target without
entering the complete function's synthetic frame.

That is not required for the JIT benefit and may alter frame observability or
increase ordinary call-cache size. Whether interpreter dispatch adopts function
specializations should therefore be decided independently from whether JIT
compilation consumes them.

## Open Design Questions

- What maximum specialization prefix length keeps call-site caches compact while
  covering the important dispatcher functions? The current operator design's
  first two shape keys are a plausible initial bound, not a settled limit.
- What is the minimum useful effect vocabulary for opaque specialized calls?
- How are semantic identities organized so they remain precise without becoming
  a second opcode enum?
- Does an interpreter cache store specialization evidence, or is the mechanism
  initially JIT-only?
- Which frame-observability features must disable specialization or force frame
  reification?

## Revisit Conditions

This direction is worth revisiting when normal function calls become a near-term
JIT target and opaque native dispatch prevents useful specialization. Before
implementation, the project should have:

1. a concrete trusted-handler metadata and VM registry design;
2. an agreed guard and invalidation contract for exact function targets;
3. one representative operation, such as integer-indexed tuple access, whose
   complete and specialized implementations can be tested against each other;
4. a decision on whether the first slice affects only JIT compilation or also
   interpreter call dispatch.
