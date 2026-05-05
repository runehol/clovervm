# Closure Cells Design Sketch

This note captures a CPython-style closure-cell representation for Python
closure variables in clovervm. In this model, a variable captured by a nested
function is represented by a stable heap cell, and all closures that capture the
same binding share that cell.

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
LOAD_DEREF:
  load cell from deref slot
  load cell.value

STORE_DEREF:
  load cell from deref slot
  store cell.value
```

Normal local access remains a direct `LOAD_FAST` / `STORE_FAST` style frame-slot
operation.

## Cell Object

A cell is a heap object with one GC-visible value field. It may outlive the frame
that created it, so the contained value must be stored with ordinary heap-member
ownership semantics rather than as a borrowed frame value.

Conceptual shape:

```cpp
class Cell : public HeapObject {
 public:
  Value value() const;
  void set_value(Value value);

 private:
  MemberValue value_;
};
```

The concrete implementation should use clovervm's existing heap layout and
ownership wrappers. The important contract is that `value_` is visible to the
runtime scanner and retains/releases like other heap object fields.

An empty cell may be needed for variables that have a cell before they have a
Python value. This should use a dedicated internal sentinel such as
`Value::not_present()` so `LOAD_DEREF` can raise the correct unbound-variable
exception when a closure reads an uninitialized binding.

## Function And Frame State

Each code object should carry enough metadata to map bytecode deref operands to
cell/free variables:

```text
cellvars: names bound in this code object and captured by nested functions
freevars: names captured from enclosing code objects
```

Each function object created with a closure stores the cells for its free
variables:

```text
Function:
  code object
  globals
  defaults
  closure cells for code.freevars
```

On function entry, closure cells are copied from the function object into the
callee frame's deref slots. This is analogous to CPython's `COPY_FREE_VARS`:
the frame gets direct indexed access to the cells expected by `LOAD_DEREF` and
`STORE_DEREF`.

For variables that are cell variables of the current function, the function
entry path or an explicit bytecode instruction creates cells in the defining
frame before any captured access can occur.

## Function Creation

When bytecode creates a nested function, it builds the closure tuple/vector from
the current frame's deref slots.

For a variable bound in the immediately enclosing function:

```text
LOAD_CLOSURE x:
  load the cell for x from the current frame
```

For a variable forwarded from an outer function through an intermediate nested
function, the intermediate function already has the cell in its free-variable
slots. Creating the deeper function reuses that same cell.

This preserves the invariant:

```text
one source-level binding -> one Cell object shared by all closures
```

## Conceptual Bytecode

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
  MAKE_CELL       x
  LOAD_CONST      10
  STORE_DEREF     x

  LOAD_CLOSURE    x
  BUILD_TUPLE     1
  LOAD_CONST      <code object inner>
  MAKE_FUNCTION   closure
  STORE_FAST      inner

  LOAD_FAST       inner
  RETURN_VALUE

inner:
  COPY_FREE_VARS  1
  LOAD_DEREF      x
  RETURN_VALUE
```

The exact opcode set does not need to match CPython, but the semantic split
should remain:

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
  COPY_FREE_VARS  1
  LOAD_DEREF      x
  LOAD_CONST      1
  BINARY_ADD
  STORE_DEREF     x
  RETURN_VALUE
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
- `LOAD_DEREF` and `STORE_DEREF` operate on cell contents, not on the frame slot
  that stores the cell reference.
- Ordinary locals that are not captured remain direct frame values.
- Cell contents are heap-member roots and are visible to safepoint scanning.
- Reading an uninitialized cell raises the appropriate Python unbound-variable
  exception.
- Function creation captures cells, not current cell contents.
- Intermediate nested functions forward cells for deeper functions when needed,
  even if the intermediate function does not directly read or write the name.
