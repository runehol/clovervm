# CloverVM Architecture

This document is the introductory map for clovervm's architecture. It explains
the major runtime ideas and points to the more detailed design notes where they
exist. It is intentionally not an implementation plan; detailed staging notes
belong in the focused documents linked from each section.

clovervm is a Python VM experiment with a register/accumulator bytecode
interpreter, tagged values, shape-based objects, inline caches, deferred
refcounting, and runtime machinery intended to support future JIT compilation.

## Design Goals

- Execute a growing Python subset with semantics close to CPython where that is
  practical and valuable.
- Keep the interpreter fast, especially around dispatch, calls, attribute
  access, small integer arithmetic, and common loops.
- Make dynamic-language optimizations explicit through shapes, inline caches,
  and validity cells.
- Avoid per-temporary refcount traffic by making interpreter frames and future
  JIT frames visible at safepoints.
- Keep bytecode, frames, exception state, and safepoint metadata structured so
  optimized native code can eventually deoptimize back into the interpreter.
- Preserve a clear distinction between Python-visible semantics and internal
  storage strategies.

## Execution Pipeline

```text
Python-like source
  -> tokenizer / parser
  -> AST
  -> bytecode compiler
  -> code object
  -> interpreter frame
  -> runtime objects
  -> future JIT tiers
```

The parser owns syntax shape. The bytecode compiler owns name binding, scope
layout, constants, control-flow lowering, and register assignment. Code objects
carry the executable bytecode and metadata needed by the interpreter, exception
handling, debugging, and future compiled code. The interpreter owns bytecode
execution and dispatch shape. The object model owns attribute lookup, class
behavior, shapes, and storage layout.

## Value Representation

`Value` is the universal runtime word. Bytecode registers, the accumulator,
object slots, container entries, native helpers, inline caches, and future JIT
code all traffic in `Value`.

The current representation is a 64-bit tagged cell. Small integers are stored
inline as signed 59-bit SMIs. Booleans, `None`, `not_present`, and
`exception_marker` are inline special values. Heap objects are represented as
tagged pointers; refcounted and interned heap allocations use distinct pointer
tag bits so common ownership checks do not need to inspect the object header.

The representation is designed around cheap SMI tests, cheap pointer tests,
cheap truthiness for inline values, and clear interaction with deferred
refcounting and safepoint root scanning.

Detailed docs:

- [Value Representation](value-representation.md)
- [Object Metadata Layout](object-metadata.md)
- [CloverVM Memory Reclamation Design](memory-allocation-reclamation.md)

## Bytecode And Calling

clovervm uses V8-inspired register/accumulator bytecode. The accumulator carries
most expression results, while registers hold locals, temporaries, call
arguments, and frame state. Common accumulator/register moves have compact
single-byte forms for the first registers.

Function calls use a defined frame/register convention. Native functions,
constructor thunks, and managed code-object entry need to preserve the same
high-level calling contract so the interpreter and future compiled entries can
share call paths and frame metadata.

Detailed docs:

- [CloverVM Function Calling Convention](function-calling-convention.md)
- [Native Function Thunks](native-function-thunks.md)
- [Constructor Thunks](constructor-thunks.md)

## Interpreter

The current interpreter is a direct bytecode executor with performance-sensitive
opcode handlers. Its dispatch loop uses Clang `musttail` to keep handler-to-
handler transfers as fixed-signature tail calls. This avoids one huge
switch-based interpreter function, which compilers tend to struggle to register
allocate well. The fixed handler argument list effectively tells the compiler
that the interpreter state should stay in the same machine registers across
opcode dispatch.

## Object Model, Shapes, And Layouts

The straightforward representation for objects in dynamic languages is an
instance dictionary: attributes live in a hash table, and every attribute access
looks up a string key. That is flexible, but it is not the shape of a fast
common-case object access.

clovervm instead tries to get the common case closer to a statically typed
object layout: attributes live in fixed slots on the object, so a successful
lookup can become a direct slot access. The lookup metadata is split from the
storage. Per-instance storage lives in the object as inline or overflow slots;
the mapping from attribute names to those slots lives in the object's shape.

Shapes are shared by many instances with the same attribute layout. Once an
access site has seen a receiver shape, it can remember the resolved slot index
and turn the common read or write into a shape check followed by direct storage
access. Shape transitions handle structural mutations such as adding or deleting
attributes. More complex lookups, especially those involving classes,
inheritance, or descriptors, add validity cells to guard the assumptions that a
shape check alone cannot express.

Detailed docs:

- [Object Model](unified-object-model.md)
- [Builtin Object Model](builtin-object-model.md)

## Inline Caches And Validity Cells

Inline caches are the mechanism that turns repeated dynamic operations into the
fast paths described above. They are used where possible for attribute reads,
attribute writes and deletes, method calls, function calls, constructor calls,
and common subscript operations.

Simple object-slot accesses can often be guarded by receiver shape alone.
Inherited lookups, method calls, constructor paths, and other operations that
depend on mutable class or namespace state also need validity cells.

A validity cell represents an assumption about mutable runtime state. Optimized
code should not trust mutable structures directly; it should trust assumptions
represented by validity cells. Mutating the world invalidates the relevant
cells. This mechanism is intended to be shared by interpreter inline caches and
future JIT code.

Detailed docs:

- [Object Model](unified-object-model.md), especially "Inline Caches and Lookup
  Validity Cells"
- [Unified Object Model Transition Plan](unified-object-model-transition-plan.md)

## Deferred Refcounting And Safepoints

clovervm's lifetime model is based on deferred refcounting. Heap/object stores
retain references, but ordinary movement through interpreter stack slots,
registers, temporaries, and the accumulator should avoid refcount traffic where
possible. Active frames are roots.

Safepoints define where the runtime must be able to discover all live
references, reconcile zero-count candidates, and reclaim memory safely. The same
root model must eventually cover interpreter frames, native helper calls,
exception unwinding, JIT frames, and deoptimization exits.

Detailed docs:

- [Refcounting and Safepoints](refcounting-and-safepoints.md)
- [CloverVM Memory Reclamation Design](memory-allocation-reclamation.md)

## Exceptions And Control Flow

Managed Python code uses exception tables and interpreter unwind state for
handler lookup, active exception binding, reraise, and continued unwinding.
Native VM helpers report failures by setting pending exception state and
returning `Value::exception_marker()`, which acts as a bridge back into the
managed unwind machinery rather than as Python-visible data.

Control-flow features such as `try`/`except`, `raise`, `for` iteration,
iterator exhaustion, and eventual traceback construction should share one
coherent exception model. The same metadata also needs to be usable by future
JIT code when reconstructing interpreter-visible frames during deoptimization or
unwind.

Detailed docs:

- [Exception Transport And Protocols](exception-transport-and-protocols.md)

## Containers, Namespaces, And Mappings

Python dictionaries, object attribute storage, globals, locals, module storage,
and mapping views are related but not interchangeable. The architecture should
preserve Python-visible mapping behavior while allowing optimized internal
string-keyed tables, shape slots, and namespace-specific storage.

Detailed docs:

- [Dictionaries](dictionaries.md)
- [Namespaces And Mapping Views](namespaces-and-mappings.md)
- [Specialized list storage design](specialized-list-storage.md)

## JIT Direction

The current system is JIT-oriented but not yet a JIT implementation. A future
JIT should consume bytecode, frame metadata, inline cache feedback, shapes,
validity cells, safepoint/root maps, and runtime helper conventions.

Guards should be expressible in terms of the same shape and validity-cell model
used by the interpreter. Deoptimization must reconstruct interpreter-visible
frames, restore pending exception state when needed, and reconcile deferred
refcount state. Runtime helper calls from generated code should obey the same
safepoint and lifetime protocol as interpreter calls.

Detailed docs:

- [Optimization Ideas](optimization-ideas.md)
- [Exception Transport And Protocols](exception-transport-and-protocols.md),
  especially "JIT Direction"
- [Refcounting and Safepoints](refcounting-and-safepoints.md)

## Core Invariants

- Every `Value` is an immediate, a tagged heap pointer, or a documented
  sentinel.
- Shape slot indexes are meaningful only under the matching shape assumption.
- Mutations that can affect lookup semantics must invalidate dependent validity
  cells.
- Heap-to-heap references obey lifetime barriers; frame temporaries may use the
  deferred refcounting model.
- At every safepoint, all live roots must be discoverable.
- Bytecode and frame metadata must remain sufficient for exceptions, debugging,
  and future deoptimization.

## Open Design Questions

- Exact JIT tiering strategy.
- Final cycle-collection and reclamation story.
- Python C API compatibility scope.
- Descriptor, metaclass, and import/module completeness.
- Concurrency and no-GIL direction.
