# Closure Cells Design

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Unified lexical scope analysis, closure cell representation, lowering, and runtime access |
| Owning layers | Parser, scope analysis, codegen, managed frames, and runtime objects |
| Validated against | `ac7eada9` (Debug and Release suites, AArch64 side exits, and Release opcode-frame checks) |
| Supersedes | N/A |

This note captures a CPython-style closure-cell representation for Python
closure variables in clovervm. In this model, a variable captured by a nested
function is represented by a heap cell, and all closures that capture the same
binding share that cell. Stable identity does not require a fixed address under
moving GC.

CloverVM supports lexical captures between functions, including forwarding and
`nonlocal`. Cell operations execute in the interpreter. The AArch64 JIT
uses its existing interpreter side exits for closure bytecodes.

## Properties

- Preserves Python lexical scoping and `nonlocal` rebinding semantics.
- Keeps ordinary locals as direct frame slots.
- Gives captured bindings stable identity for the lifetime of all closures that
  reference them.
- Avoids open/closed upvalue transitions and frame-exit closing operations.
- Uses ordinary traced object references for cell contents and closure tuples.
- Uses the existing JIT accumulator snapshot and interpreter recovery path.

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

[`ScopeAnalyzer`](../src/compiler/codegen_scope_analysis.cpp) collects each
body's declarations, parameters, bindings, referenced names, and child
definitions. It resolves references against enclosing function scopes as it
descends. A child can promote an enclosing binding to `Cell` and add `Free`
bindings to intervening functions. Each scope assigns slots and runs flow
analysis after visiting its children. Codegen starts after this analysis
has finished for the whole compilation.

### Scope Results And Lifetime

`CompilationScopeAnalysis` indexes each `ScopeAnalysis` by its body AST node.
The result contains the binding classifications, named bindings in frame-slot
order, and an ordered list of free bindings. Each free binding records its
own binding index and the binding index of the supplying cell in the immediate
parent.

The compilation owns one set of load, store, and delete annotations indexed by
AST node ID. Each body's flow analysis fills the entries for accesses it
analyzes, including their presence information. Codegen reads those entries by
node ID. Analysis of a nested definition's defaults fills their entries in the
enclosing scope, where the defaults execute. With one array per access kind,
annotation storage scales with the size of the AST regardless of how many
scope results are retained.

The analyzer uses four binding classifications:

```cpp
enum class BindingScope {
    Local,
    Cell,    // own binding captured by a child
    Free,    // enclosing function binding, including forwarding
    Global,
};
```

The compiler uses these records to build closure tuples in free-variable
order. Code objects retain the free-variable count and relevant names.

### Collection And Resolution

Binding collection scans the whole body, including unreachable statements and
conditional assignments. A nested function's body belongs to the child.
Its default expressions execute in the enclosing scope, where the definition
binds the function's name.

A simple bare annotation such as `x: int` in a function declares a local
without assigning a value. The parser distinguishes simple and non-simple
annotated assignments with separate AST node kinds. A bare annotation records
no write in flow analysis and emits no store. If a nested function captures the
binding, its cell starts empty with `Value::not_present()`. A bare parenthesized
annotation such as `(x): int` does not declare a local.

Collection and capture resolution work with binding indices. Slot assignment
follows resolution.

1. Install explicit globals and bind parameters. Resolve explicit
   `nonlocal` declarations as `Free` before collecting assignments, then
   collect the function's own bindings as `Local`.
2. Resolve remaining referenced names to enclosing function bindings (`Free`)
   or global lookup (`Global`). Own bindings shadow enclosing bindings
   throughout the function, including accesses before the assignment.
3. Resolve child scopes with the updated visible binding environment.
4. Propagate child capture requirements upward. An own local needed by a child
   becomes `Cell`. A binding from farther out becomes `Free` here as well, so
   this function forwards its cell even if it does not itself access the name.

An explicit `global` in an intermediate function blocks capture of the outer
binding through that scope.

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

A single declaration pass validates `global` and `nonlocal` in source order
and records directives and referenced names. `DeclarationState` records one
declaration kind per name:

```cpp
enum class DeclarationKind {
    None,
    Global,
    Nonlocal,
};
```

The validator uses parameter, use, assignment, and annotation facts to detect
parameter conflicts, use or assignment before a declaration, annotation
conflicts, and names declared both `global` and `nonlocal`. Validation remains source ordered even
though valid declarations affect binding classification throughout the scope.

The declaration pass uses the annotated-assignment node kind. A simple
annotation such as `x: int` counts as a name annotation
for conflict checks. Bare `(x): int` contributes neither an annotation conflict
nor a name use or assignment. `(x): int = value` counts as an ordinary
assignment to `x`. For example, this assignment may follow `nonlocal x`, but
placing it before `nonlocal x` triggers the prior-assignment error.

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

The parser uses a shared name-list routine for `global` and `nonlocal`.
These declarations emit no runtime instructions. Name accesses use the global
or cell operations selected by scope analysis.

### Layout And Flow

After resolution, the analyzer assigns named slots using the existing
parameter, padding, and frame-header layout. Free variables come first in the
below-FP locals area, followed by own locals. Codegen installs names in frame
slot order and allocates temporary registers. Frame layouts with cells that
exceed the signed bytecode register range raise `SyntaxError`.

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

CloverVM supports captures between function scopes without an intervening
class body. During lexical resolution, class namespace bindings
are skipped when looking for an enclosing function binding for a method.
If the resolved binding would require a cell to enter or pass through a class
body, compilation raises `SyntaxError` with the message
`closures across class bodies are not supported`. This covers a class body
accessing an enclosing function's binding and a method capturing that binding
through the class body. Names that resolve as globals use global lookup. A nested function
inside a method can still capture the method's own locals.

Class-body free-variable transport and lookup are deferred to the live class
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
field. [`Cell`](../src/object_model/cell.h) uses the internal builtin class
and native-layout registration. Its `Member<Value>` owns the contained value
when the cell outlives the defining frame.

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

The native layout includes `value_` in the traced value span. Stores and
deletes use ordinary heap-member retain and release semantics.

A recursive nested function can form a cycle through its closure tuple and
the cell for its own name. The function, tuple, and cell hold ordinary strong
references. Reclaiming unreachable cycles is part of the full tracing
collection in the [moving GC design](generational-copying-gc.md).

Python-visible introspection remains deferred, including
`function.__closure__`, `cell.cell_contents`, `types.CellType`, and
`co_cellvars` and `co_freevars`. The runtime scope table retains binding names
for empty-cell errors.

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

A `CodeObject` stores `n_freevars` and uses its existing `Scope` slot table
for parameter and local names. A `Function` owns its optional closure tuple
in a reserved, traced inline slot alongside its code object, defaults, and
docstring. Construction checks that a closure has exactly `n_freevars` entries
and that every entry is a `Cell`. A closure-free function has no tuple.

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
  initialize the callee header
  set accumulator to the selected Function and switch fp, code_object, and pc
  honor a requested entry safepoint, preserving the accumulator

callee:
  Enter n_slots                         # clears below-FP slots, preserves acc
  CopyFreeVars                          # only when code.n_freevars > 0
  MakeCell <each captured own parameter/local slot>
  execute the body
```

The existing interpreter call-entry poll runs after argument binding and the
switch to the callee, before `Enter`. The accumulator-preserving safepoint
path publishes the selected `Function` as a live root and reloads it before
dispatching `Enter`. The entry accumulator
contract starts after call adaptation.

The closure-reference copy must precede any operation that overwrites the entry
accumulator. It performs no allocation. Once copied, the cells are rooted in
their frame slots and the accumulator can be reused. `Enter`, `CopyFreeVars`,
and `MakeCell` run without polling. `MakeCell` leaves the initial value in its
slot through construction, then writes the new cell reference to the slot
before dispatching the next instruction. The allocator can request a later
safepoint.

Positional, keyword, expanded, method, and native-to-managed calls use this
entry contract. The accumulator
contains the selected function, which may be an adapter chosen during call
resolution. Resuming an existing frame skips this entry initialization.

Operator dispatch through continuations follows the same entry contract when
it calls a Python function. Both cached and uncached dispatch put the exact
selected `Function` in the accumulator after argument adaptation and before
dispatching to the callee. The operator continuation keeps its existing
return and safepoint behavior.

### Direct Code-Object Calls From Constructor Thunks

`CallCodeObject` enters the target with already-bound arguments and preserves
the accumulator. The `__init__`-only and `__new__`-only thunk builders keep
the exact target `Function` in the thunk's constant table when the target
has free variables. After preparing the arguments, the builders emit
`LdaConstant target_function` immediately before `CallCodeObject`. The target
function owns its closure tuple, and the callee's `CopyFreeVars` reads it
through the accumulator after `Enter`.

The thunk builder decides whether to emit the load using
`target_code.n_freevars > 0`. Targets without free variables retain their
existing bytecode sequence. `CallCodeObject` uses the existing operand format
and handler for both cases. Every direct call to code with free variables must
supply the matching function instance through this entry contract.

The bytecode effects model declares the accumulator as an unconditional
input to `CallCodeObject` and as the destination for its result.
This conservatively models the incoming value for JIT analysis. The callee
uses it as a function only when it has free variables.

## JIT Entry Accumulator And Side Exits

The JIT models the incoming bytecode accumulator as a tagged SSA entry
parameter.
For fresh function calls, that input carries the selected `Function` under the
same entry contract as the interpreter. It is per-invocation state, not a
function constant associated with the compiled code object.

The AArch64 interpreter-to-JIT entry thunks carry the accumulator in `x20`
into generated code. The compiler connects it to entry state as follows.

1. `CoreBytecodeTranslator::make_entry_state` adds a tagged
   `ParameterInstruction` after the Python parameter and frame-header inputs.
2. `BytecodeStateTracker::make_entry_state` puts that SSA value at
   `BytecodeStateOrder::AccumulatorPosition`.
3. The AArch64 entry constraints assign the input to `x20`. Python arguments
   retain their existing locations and signature arity.

The tracker initializes the accumulator when it builds entry state.

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

After an instruction overwrites the accumulator, subsequent snapshots capture
the new value. The JIT's logical `Enter` initializes locals and preserves
the accumulator. A side exit at `CopyFreeVars` restores the selected function
and cleared locals before the interpreter resumes the copy.

Tests cover immediate fallback at `CopyFreeVars`, recovery after overwriting
the accumulator, and separate functions sharing code with distinct closures.

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

## Closure Opcodes

The interpreter implements seven closure opcodes. Register operands can
address parameter or local slots. Constant operands identify code objects in
the current code object's constant table.

| Opcode | Operands | Instruction bytes | Effect |
|---|---|---|---|
| `CopyFreeVars` | None | 1 | Copy `code.n_freevars` cell references from the entry accumulator's `Function` closure tuple into the locals prefix, preserving the accumulator. |
| `MakeCell` | Register | 2 | Replace the slot with a new cell containing its previous value; preserve the accumulator. |
| `LoadCellChecked` | Register | 2 | Load the cell contents into the accumulator; raise the appropriate empty-cell error. |
| `StoreCell` | Register | 2 | Store the accumulator into the cell contents with heap-member ownership semantics; preserve the accumulator. |
| `DelCell` | Register | 2 | Set the cell contents to `not_present`; raise the appropriate empty-cell error if already empty. |
| `CreateFunctionWithClosure` | Code constant, closure register | 3 | Construct a complete function with the closure tuple and put it in the accumulator. |
| `CreateFunctionWithDefaultsAndClosure` | Code constant, defaults register, closure register | 4 | Construct a complete function with defaults and closure tuples and put it in the accumulator. |

`CopyFreeVars` takes its count from the code object rather than duplicating it
in a bytecode operand. It performs no allocation. Codegen emits it immediately
after `Enter` for functions with free variables.

`MakeCell` allocates one cell and wraps the slot's existing value. The same
operation handles a bound parameter and an own local cleared to `not_present`
by `Enter`. It neither reads the entry function from the accumulator nor writes
its result there.

Every `LoadCellChecked` checks for empty contents. Shared cell contents can
change during calls, so local presence analysis leaves these checks in place. `DelCell`
uses the same own-binding versus free-variable error classification. Neither
stores nor deletion replace the cell reference in the frame slot.

Automatic exception-handler binding cleanup follows the existing assignment
then deletion pattern, using `LdaNone; StoreCell binding; DelCell binding`.
This succeeds even if the handler already deleted the binding. A separate
`ClearCell` opcode is not needed initially.

Closure-free functions use `CreateFunction` or `CreateFunctionWithDefaults`. The four construction forms are:

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

  Mov             capture0, x    # copy the cell reference
  CreateTuple     capture0, 1
  Star            capture0
  CreateFunctionWithClosure <inner code constant>, capture0
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
