# Closure Cells Design Sketch

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started |
| Scope | Unified lexical scope analysis, closure cell representation, lowering, and runtime access |
| Owning layers | Parser, scope analysis, codegen, managed frames, and runtime objects |
| Validated against | N/A |
| Supersedes | N/A |

This note captures a CPython-style closure-cell representation for Python
closure variables in clovervm. In this model, a variable captured by a nested
function is represented by a heap cell, and all closures that capture the same
binding share that cell. Stable identity does not require a fixed address under
moving GC.

The unified scope analysis, storage, interpreter/JIT entry and recovery design,
and opcode plan below are agreed. Implementation is not authorized yet. Native
compiled lowering of the cell operations is not required by the entry plan;
unsupported operations can use the existing interpreter side-exit mechanism.

## Properties

- Preserves Python lexical scoping and `nonlocal` rebinding semantics.
- Keeps ordinary locals as direct frame slots.
- Gives captured bindings stable identity for the lifetime of all closures that
  reference them.
- Avoids open/closed upvalue transitions and frame-exit closing operations.
- Keeps closure state directly compatible with Python-visible cell
  introspection.
- Provides a simple representation for interpreter execution, future JIT
  materialization, frame scanning, and deoptimization.

## Scope Classification

The compiler classifies names before register assignment:

- A **local** is bound and used only inside the current function.
- A **cell variable** is bound in the current function and referenced by at
  least one nested function.
- A **free variable** is not bound in the current function, but is resolved from
  an enclosing function scope.
- A **global** or **builtin** is resolved outside the function-closure chain.

For example:

```python
def outer():
    x = 0

    def get():
        return x

    def inc():
        nonlocal x
        x += 1

    return get, inc
```

`x` is a cell variable in `outer` and a free variable in both `get` and `inc`.
The two nested functions must share one binding. Rebinding through `inc` must be
visible through `get`.

## Unified Scope Analysis

Extend the existing analyzer in
[`codegen_scope_analysis.cpp`](../src/compiler/codegen_scope_analysis.cpp) and
its `ScopeAnalysis` results. Do not introduce a parallel lexical analysis system
or a competing symbol table. The analyzer owns one scope tree for the
compilation, with one result per eventual code object. Code objects do not need
to exist while lexical bindings are resolved.

The current per-code-object analysis starts when codegen enters each body.
Closure classification instead needs the complete nested scope information
before any body is lowered: a nested function may require an earlier access in
its parent to use a cell. The unified sequence is:

```text
existing ScopeAnalyzer:
  collect the scope tree and declaration/binding facts
  resolve Local / Cell / Free / Global throughout the tree
  assign each scope's named slots
  run existing flow/presence analysis separately for each body

codegen:
  recursively emit code objects using their completed scope results
```

`AstCodegen::make` receives the corresponding completed analysis rather than
starting an independent analysis for every code object. Recursive code-object
emission can remain; it must not rediscover lexical bindings.

### Scope Results And Lifetime

Extend each existing `ScopeAnalysis` result with its scope AST node and mode,
child scopes, binding classifications, and ordered free-variable bindings.
Retain its existing load/store/delete annotations and presence information.
Conceptually, `BindingScope` becomes:

```cpp
enum class BindingScope {
    Local,
    Cell,    // own binding captured by a child
    Free,    // enclosing function binding, including forwarding
    Global,
};
```

For each child's free variable, record which binding in the immediate parent
supplies the cell. This is the closure-construction mapping, including names
forwarded through intermediate functions. Resolve binding identities before
assigning their slots; all children capturing one own binding identify the
same binding and therefore the same cell for an activation.

These are compiler records owned for the duration of compilation, not runtime
scope environments. Code objects receive only the runtime metadata they need,
including `n_freevars` and relevant names. No runtime parent-frame search is
introduced.

### Collection And Resolution

Reuse and extend binding collection and declaration validation to collect each
scope's parameters, own bindings, references, `global` / `nonlocal`
declarations, and child scopes. Scan the whole body regardless of reachability
or conditional assignment. A nested function's body belongs to the child;
default-value expressions are analyzed in the enclosing scope, where they
execute. The function definition's name is a binding in the enclosing scope.

Collection does not assign slots. In particular, decouple
`ensure_local_binding()` from its current immediate slot allocation through
`CodeObjectBuilder`.

Resolve each scope with its enclosing binding environment:

1. Apply explicit declarations and establish own bindings. `global` selects
   global lookup; `nonlocal` requires an enclosing function binding and selects
   `Free`. Other bindings in this function start as `Local`.
2. Resolve remaining referenced names to enclosing function bindings (`Free`)
   or global lookup (`Global`). Own bindings shadow enclosing bindings
   throughout the function, including accesses before the assignment.
3. Resolve child scopes with the updated visible binding environment.
4. Propagate child capture requirements upward. An own local needed by a child
   becomes `Cell`. A binding from farther out becomes `Free` here as well, so
   this function forwards its cell even if it does not itself access the name.

The downward environment identifies the binding; the upward requirements
identify which bindings need shared storage. Declaration validation must retain
syntax errors for unresolved `nonlocal`, conflicting declarations, parameter
conflicts, and invalid declaration ordering. An explicit `global` in an
intermediate function blocks capture of the outer binding through that scope.

For example:

```python
def outer(x):
    def middle():
        def inner():
            return x
        return inner
    return middle
```

| Scope | Classification of `x` | Closure source |
|---|---|---|
| `outer` | `Cell` parameter | Bound argument |
| `middle` | `Free`, locals-prefix slot 0 | `outer`'s parameter cell |
| `inner` | `Free`, locals-prefix slot 0 | `middle`'s free-variable slot 0 |

### Shared `global` And `nonlocal` Declaration Handling

The existing analyzer already validates `global` declarations in source order
and collects them before classifying own bindings. Extend that same machinery
to handle `nonlocal`; do not add a separate declaration analysis. Generalize
the existing `GlobalDeclarationState` and its validation/collection helpers to
shared declaration handling, with a per-name declaration kind:

```cpp
enum class DeclarationKind {
    None,
    Global,
    Nonlocal,
};
```

Retain the existing per-name parameter, use, assignment, and annotation facts.
Use them to validate both declaration kinds, including parameter conflicts,
use or assignment before a declaration, annotation conflicts, and a name
declared both `global` and `nonlocal`. Validation remains source ordered even
though valid declarations affect binding classification throughout the scope.

| Declaration | Binding resolution |
|---|---|
| `global x` | Classify `x` as `Global`; reads, writes, and deletion use module lookup/storage rather than an enclosing function binding. |
| `nonlocal x` | Resolve the nearest visible enclosing function binding and classify `x` as `Free`; no such binding is a `SyntaxError`. |

A `nonlocal` declaration prevents assignments to that name from creating an
own local. It also creates a capture requirement even if the function only
writes the name, or declares it without reading it. Propagation promotes the
owning function's binding to `Cell` and forwards the cell through any
intermediate functions. Module bindings do not satisfy `nonlocal`, and a
module-level `nonlocal` declaration is invalid.

For example:

```python
def outer():
    x = 0
    def inner():
        nonlocal x
        x = 1
    return inner
```

`outer.x` is `Cell` and `inner.x` is `Free`, despite the absence of a read in
`inner`. Its assignment lowers to `StoreCell` for the captured cell slot.

The parser's currently unimplemented `nonlocal_stmt()` should produce the
existing `STATEMENT_NONLOCAL` AST kind with the same name-list shape as
`global`. Both statement kinds emit no runtime instructions: their effects are
resolved by analysis, which selects global or cell operations for accesses.

### Layout And Flow

After resolution, assign named slots using the existing parameter, padding,
and frame-header layout. Put free variables first in the below-FP locals area,
followed by own locals, as described below. The builder consumes this named
layout rather than independently allocating or rediscovering bindings.
Temporary register allocation remains codegen's responsibility.

Run the existing flow analysis separately for each body after classification
and layout. Evaluating a nested definition's defaults and binding its name
belong to the parent's flow; executing its body does not. Each child body has
its own initial flow state.

Keep cell-reference presence distinct from cell-content presence: an
initialized free-variable slot holds a valid cell even when its contents are
`not_present`. The parent's flow state at closure creation does not establish
what the cell will contain when the child eventually executes. Ordinary local
presence analysis must not eliminate `LoadCellChecked` checks.

Module and class scopes remain explicit nodes with their own binding rules.
Module bindings are not closure cells, and class namespace bindings must not
be treated as ordinary enclosing function bindings for methods.

### Captures Across Class Bodies

The first closure slice supports captures between function scopes without an
intervening class body. During lexical resolution, class namespace bindings
are skipped when looking for an enclosing function binding for a method.
If the resolved binding would require a cell to enter or pass through a class
body, reject the capture at compile time with an explicit unsupported-feature
diagnostic. This covers both a class body accessing an enclosing function's
binding and a method capturing that binding through the class body. Preserve
ordinary global lookup for names that resolve as globals. A nested function
inside a method can still capture the method's own locals.

Defer this transport and class-body free-variable lookup to the live class
namespace work described in the
[module-global namespace design](module-global-namespace-design.md#remaining-design-boundaries).
That work must keep namespace entries separate from forwarded cells, including
when both have the same name. Special `__class__` cells and zero-argument
`super()` remain separate design work.

## Runtime Model

Ordinary locals are stored directly in frame slots:

```text
ordinary local:
  frame slot -> Value
```

Captured locals are stored through a cell:

```text
cell variable in defining frame:
  frame slot -> Cell -> Value

free variable in nested frame:
  frame/freevar slot -> same Cell -> Value
```

The cell is the shared binding container. Closures capture cells, not snapshots
of values. A write to a captured variable updates the cell contents; every
closure that references that cell observes the new value.

The defining frame and nested frames therefore use the same access path for
captured bindings:

```text
LoadCellChecked slot:
  load cell from deref slot
  load cell.value

StoreCell slot:
  load cell from deref slot
  store cell.value
```

Normal local access remains a direct `Ldar` / `Star` frame-slot operation, with
`LoadLocalChecked` where a presence check is needed.

## Cell Object

A cell is a Python object deriving from `Object`, with one GC-visible value
field. Use the existing builtin class, native-layout, allocation, and ownership
patterns for `Object` subclasses rather than an internal-only `HeapObject`
record. It may outlive the frame that created it, so the contained value must
be stored with ordinary heap-member ownership semantics rather than as a
borrowed frame value.

Conceptual shape:

```cpp
class Cell : public Object {
 public:
  Value value() const;
  void set_value(Value value);

 private:
  Member<Value> value_;
};
```

The concrete implementation should use clovervm's existing heap layout and
ownership wrappers. The important contract is that `value_` is visible to the
runtime scanner and retains/releases like other heap object fields.

A recursive nested function can form a cycle through its closure tuple and
the cell for its own name. The function, tuple, and cell hold ordinary strong
references. Reclaiming unreachable cycles is part of the full tracing
collection in the [moving GC design](generational-copying-gc.md).

The Python object representation supports CPython-compatible introspection:
`Function.__closure__` exposes a read-only tuple of the actual shared cells
(or `None` without a closure), while a cell's `cell_contents` can be read,
assigned, or deleted. Assigning contents changes the binding observed by all
closures sharing the cell; deletion empties it. Reading an empty cell through
`cell_contents` raises `ValueError`, distinct from the binding-access errors
described below. The function's immutable closure association does not make
the cell contents immutable.

The first slice uses the `Object` representation and defers Python-visible
introspection. A follow-up can expose `function.__closure__`,
`cell.cell_contents`, `types.CellType`, and code-object attributes such as
`co_cellvars` and `co_freevars`. Keep the internal binding names needed for
error reporting in the first slice.

An empty cell contains `Value::not_present()`. Cell creation is unconditional at
function entry for each of the activation's captured bindings, even when the
assignment or nested function definition is only reached on some paths. Captured
parameters are boxed in their existing parameter slots, initialized with the
bound argument. Captured non-parameter locals are boxed in their own local
slots, initialized with `not_present`. Subsequent reads and writes in the
defining function also access the cell contents. Uncaptured locals remain direct
values; there is no lazy promotion or open/closed storage transition.

For example:

```python
def outer(x):
    if x > 0:
        y = 0
    def inner():
        return y
    return inner
```

`outer(0)` succeeds and returns a function capturing an empty cell. Calling that
function raises `NameError`. Reading the same empty binding inside `outer`
raises `UnboundLocalError`. Capturing an empty cell is valid, and a later
`nonlocal` assignment can populate it. Deletion clears the contents back to
`not_present` without replacing the shared cell.

## Function And Frame State

Each code object carries the expected free-variable count, `n_freevars`, and
enough name and slot metadata to map deref accesses to cell/free variables:

```text
cellvars: names bound in this code object and captured by nested functions
freevars: names captured from enclosing code objects
n_freevars: expected number of cells in the function's closure
```

Each function object created with a closure stores the cells for its free
variables:

```text
Function:
  code object
  defaults
  closure cells for code.freevars
```

Closure cells belong to the function instance, not the code object. Separate
activations of an enclosing function can produce functions sharing one code
object while capturing different cells.

Free variables occupy the beginning of the callee's locals, separately from
the existing parameter slots:

```text
parameters: existing parameter slots, including boxed captured parameters

locals:
  [0, n_freevars)      references to captured free-variable cells
  [n_freevars, ...)    own locals, including boxed captured locals
  ... temporaries
```

The closure ordering matches this prefix. Entry copies `n_freevars` references
to existing cells, not their contents or newly allocated cells. The frame then
has direct indexed access to each shared cell.

An empty-cell error uses the accessing code object's slot classification:

- A cell in the free-variable locals prefix raises `NameError`.
- An own local or parameter cell raises `UnboundLocalError`.

This is a defining-scope versus enclosing-scope distinction, not a parameter
versus local distinction. Classification uses logical local indices, accounting
separately for parameter slots; encoded frame offsets must not be compared
directly with `n_freevars`. The cell needs no owner-scope flag.

## Interpreter Entry Transport

The selected `Function` is passed in the accumulator at fresh function entry.
This reuses the existing interpreter dispatch signature and does not add a
permanent function field to the frame header. The sequence is:

```text
caller:
  finish call adaptation and argument binding
  set accumulator to the selected Function
  initialize the callee header and switch fp, code_object, and pc
  honor a requested entry safepoint, preserving the accumulator

callee:
  Enter n_slots                         # clears below-FP slots, preserves acc
  CopyFreeVars                          # only when code.n_freevars > 0
  MakeCell <each captured own parameter/local slot>
  execute the body
```

The existing interpreter call-entry poll runs after argument binding and the
switch to the callee, before `Enter`. Use the accumulator-preserving safepoint
path so it publishes the selected `Function` as a live root and reloads its
possibly relocated value before dispatching `Enter`. The entry accumulator
contract starts after call adaptation.

The closure-reference copy must precede any operation that overwrites the entry
accumulator. It performs no allocation. Once copied, the cells are rooted in
their frame slots and the accumulator can be reused. `Enter`, `CopyFreeVars`,
and `MakeCell` run without polling. `MakeCell` leaves the initial value in its
slot through construction, then writes the new cell reference to the slot
before dispatching the next instruction. The allocator can request a later
safepoint.

All fresh `Function` entry paths, including positional, keyword, expanded,
method, and native-to-managed calls, must honor this contract. The accumulator
contains the selected function, which need not be the original callable when
resolution chooses an adapter. Internal direct code-object entry has no
function instance and must not silently enter code requiring closure context.
Resuming an existing frame skips this entry initialization.

## JIT Entry Accumulator And Side Exits

Model the incoming bytecode accumulator as a real tagged SSA entry parameter,
rather than initializing its state-tracker position to the unavailable value.
For fresh function calls, that input carries the selected `Function` under the
same entry contract as the interpreter. It is per-invocation state, not a
function constant associated with the compiled code object.

The current AArch64 interpreter-to-JIT entry thunks already receive the
accumulator in `x20` and leave it intact when calling generated code. Preserve
that transport and connect it to the compiler's entry model:

1. In `CoreBytecodeTranslator::make_entry_state`, create an ordinary tagged
   `ParameterInstruction` for the incoming accumulator alongside the Python
   parameter and frame-header inputs.
2. Pass this SSA value explicitly to `BytecodeStateTracker::make_entry_state`
   and assign it to `BytecodeStateOrder::AccumulatorPosition`. Keep the existing
   initialization of locals and temporaries.
3. Extend AArch64 entry-parameter allocation constraints to recognize this
   input and give it the fixed incoming location `x20`. It is not an extra
   Python argument and does not shift Python argument locations `x0` through
   `x7` or alter signature arity.

Conceptually, the tracker initialization becomes:

```cpp
make_entry_state(parameters, frame_header, incoming_accumulator,
                 uninitialized_local, unavailable);

// Within the tracker:
values[BytecodeStateOrder::AccumulatorPosition] = incoming_accumulator;
```

The fixed location applies only at entry. `x20` remains allocatable during
compiled execution: the allocator can move or preserve the SSA value using its
normal location and liveness machinery. The function reference is not a new
persistent machine-context register or frame-header field.

Side exits use the existing accumulator recovery path:

```text
entry x20 contains Function
  -> tagged entry SSA parameter
  -> state.accumulator
  -> Snapshot captures the current accumulator value
  -> transition program materializes InterpreterResumeState.accumulator
  -> side-exit thunk restores the recovered value into x20
  -> interpreter resumes at the snapshot's bytecode PC
```

The transition emitter already materializes the snapshot accumulator, and the
side-exit thunk already restores it. No closure-specific recovery field or
separate function-recovery mechanism is needed. After an instruction overwrites
the accumulator, later snapshots must capture that newer value rather than the
entry function.

This permits the first closure implementation to leave `CopyFreeVars`
unsupported in native compiled lowering. The JIT's existing logical `Enter`
initialization leaves the modeled accumulator unchanged; an ordinary side exit
at `CopyFreeVars` then restores the function and cleared local state and resumes
at that instruction. The interpreter copies the cells without rerunning
`Enter`. Keeping all closure-bearing functions interpreted is therefore not
required merely to preserve the entry function.

Future compiled-to-compiled call lowering must supply the same entry input in
the agreed machine location. This plan does not add general managed JIT calls
or safepoint-capable cell lowering.

Verification must cover an immediate fallback at `CopyFreeVars`, an exit after
the accumulator has been overwritten, and separate function instances sharing
one code object but carrying different closures. Entry constraints and recovery
must work without reserving `x20` throughout the compiled body.

## Function Creation

When bytecode creates a nested function, it builds a closure tuple from the
current frame's cell references in the order expected by the nested code
object. Existing `Ldar` / `Mov` instructions read or copy the cell reference
itself; `LoadCellChecked` reads its contents. No separate `LoadClosure` opcode is
needed. Existing `CreateTuple` assembles the tuple from temporary registers.

The closure tuple is passed to the `Function` constructor along with its code
object and any defaults. A function is fully initialized before publication;
there is no `SetFunctionClosure` operation. Its closure association remains
immutable while the shared cells' contents can change.

For a variable forwarded from an outer function through an intermediate nested
function, the intermediate function already has the cell in its free-variable
slots. Creating the deeper function reuses that same cell.

This preserves the invariant:

```text
one source-level binding -> one Cell object shared by all closures
```

## Opcode Plan

Add seven opcodes. Register operands use the existing encoded register format
and can address parameter or local slots. Constant operands identify code
objects in the current code object's constant table.

| Opcode | Operands | Instruction bytes | Effect |
|---|---|---|---|
| `CopyFreeVars` | None | 1 | Copy `code.n_freevars` cell references from the entry accumulator's `Function.closure` into the locals prefix. |
| `MakeCell` | Register | 2 | Replace the slot with a new cell containing its previous value; preserve the accumulator. |
| `LoadCellChecked` | Register | 2 | Load the cell contents into the accumulator; raise the appropriate empty-cell error. |
| `StoreCell` | Register | 2 | Store the accumulator into the cell contents with heap-member ownership semantics; preserve the accumulator. |
| `DelCell` | Register | 2 | Set the cell contents to `not_present`; raise the appropriate empty-cell error if already empty. |
| `CreateFunctionWithClosure` | Code constant, closure register | 3 | Construct a complete function with the closure tuple and put it in the accumulator. |
| `CreateFunctionWithDefaultsAndClosure` | Code constant, defaults register, closure register | 4 | Construct a complete function with defaults and closure tuples and put it in the accumulator. |

`CopyFreeVars` takes its count from the code object rather than duplicating it
in a bytecode operand. It performs no allocation. Emit it immediately after
`Enter` when free variables exist, before any accumulator overwrite.

`MakeCell` allocates one cell and wraps the slot's existing value. The same
operation handles a bound parameter and an own local cleared to `not_present`
by `Enter`. It neither reads the entry function from the accumulator nor writes
its result there.

Initially every `LoadCellChecked` checks for empty contents. No unchecked load variant
is planned: shared cell contents can change during calls, so ordinary local
presence reasoning alone is insufficient to eliminate the check. `DelCell`
uses the same own-binding versus free-variable error classification. Neither
stores nor deletion replace the cell reference in the frame slot.

Automatic exception-handler binding cleanup follows the existing assignment
then deletion pattern, using `LdaNone; StoreCell binding; DelCell binding`.
This succeeds even if the handler already deleted the binding. A separate
`ClearCell` opcode is not needed initially.

Keep the existing `CreateFunction` and `CreateFunctionWithDefaults` operations
for functions without closures. The four construction forms are:

| Defaults | Closure | Opcode |
|---|---|---|
| No | No | `CreateFunction` |
| Yes | No | `CreateFunctionWithDefaults` |
| No | Yes | `CreateFunctionWithClosure` |
| Yes | Yes | `CreateFunctionWithDefaultsAndClosure` |

The two new handlers load the supplied tuple references and pass them directly
to construction, following the existing defaults handler. Closure assembly
uses existing register and tuple operations, and `nonlocal` has no dedicated
runtime opcode: binding analysis selects the appropriate cell slot.

## Bytecode Example

For:

```python
def outer():
    x = 10

    def inner():
        return x

    return inner
```

The conceptual bytecode shape is:

```text
outer:
  Enter           <padded local/temporary slot count>
  MakeCell        x
  LdaSmi          10
  StoreCell       x

  Ldar            x              # cell reference, not contents
  Star            capture0
  CreateTuple     capture0, 1
  Star            closure_tuple
  CreateFunctionWithClosure <inner code constant>, closure_tuple
  Star            inner

  Ldar            inner
  Return

inner:
  Enter           <padded local/temporary slot count>
  CopyFreeVars                    # count is code.n_freevars == 1
  LoadCellChecked        free0           # x's shared cell in the locals prefix
  Return
```

Slot names above stand for compiler-assigned encoded registers, not extra
bytecode metadata. The semantic split is:

- ordinary locals use direct frame-slot opcodes;
- captured bindings use deref opcodes over cells;
- function creation captures cells, not values.

## `nonlocal`

`nonlocal` changes name binding, not runtime storage. A name declared
`nonlocal` resolves to a cell from an enclosing function scope. Reads and writes
therefore use deref operations:

```python
def outer():
    x = 0

    def inc():
        nonlocal x
        x += 1

    return inc
```

Conceptually:

```text
inc:
  Enter           <padded local/temporary slot count>
  CopyFreeVars                    # count is code.n_freevars == 1
  LoadCellChecked        free0           # x
  ... existing augmented-add lowering for x += 1 ...
  StoreCell       free0
  LdaNone
  Return
```

Without `nonlocal`, an assignment in the nested function binds a new local name
in that nested function. It does not write the outer cell.

## Late Binding

Closures capture bindings, not values. If several closures capture the same
cell, they observe the cell's current contents when called.

This matches Python's late-binding behavior:

```python
def make_funcs():
    funcs = []
    for i in range(3):
        def f():
            return i
        funcs.append(f)
    return funcs
```

All closures produced by this function capture the same binding for `i` in the
surrounding function scope. They do not receive per-iteration value snapshots.

## Comparison To Open Upvalues

Lua-style upvalues keep captured locals in ordinary frame slots while the
defining frame is active, then close the upvalue into heap storage when that
frame exits:

```text
open:
  closure -> upvalue -> defining frame slot

closed:
  closure -> upvalue -> upvalue-owned storage
```

That representation keeps the defining function's captured local access direct,
but closure access goes through an upvalue and a mutable storage pointer. It also
requires the VM to track open upvalues and close them before frame storage
becomes invalid.

The cell model chooses a stable representation instead:

```text
defining frame -> cell -> value
nested closure -> same cell -> value
```

Captured variables pay one extra indirection in both the defining function and
nested functions. In exchange, the runtime avoids storage-mode transitions,
open-upvalue lists, frame-exit closing, and cached-stack-slot invalidation. This
is a good fit for Python semantics, where closure cells are visible through
function introspection and where frames, generators, tracebacks, and future JIT
deoptimization all benefit from stable materializable state.

## Implementation Invariants

- A source-level binding captured by nested functions is represented by exactly
  one cell object per function activation.
- All closures that capture that binding reference the same cell.
- Captured own bindings are boxed unconditionally at entry in their existing
  parameter/local slots; empty contents use `Value::not_present()`.
- Free-variable cell references occupy the locals prefix of length
  `CodeObject::n_freevars`; the function instance owns the corresponding closure.
- Fresh interpreter function entry carries the selected `Function` in the
  accumulator through `Enter` and any intervening safepoint until closure cells
  have been copied into the frame.
- `LoadCellChecked` and `StoreCell` operate on cell contents, not on the frame slot
  that stores the cell reference.
- Ordinary locals that are not captured remain direct frame values.
- Cell contents are heap-member roots and are visible to safepoint scanning.
- Reading an empty own local/parameter cell raises `UnboundLocalError`; reading
  an empty free-variable cell raises `NameError`.
- Function creation captures cells, not current cell contents.
- Intermediate nested functions forward cells for deeper functions when needed,
  even if the intermediate function does not directly read or write the name.
