# Closure Cells Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Not started |
| Scope | Four preparatory stages and six feature commits for function closures |
| Owning layers | Parser, scope analysis, bytecode and codegen, interpreter, JIT entry and recovery, runtime objects |
| Validated against | N/A |
| Supersedes | N/A |

The [closure design](closure-cells.md) defines the storage and execution
contracts. This plan orders the work and gives each stage a verification
checkpoint. Implementation requires separate authorization.

## Preparatory Stages

1. **Correct annotation binding and declaration facts.** Collect a simple
   bare `x: T` in a function as a local without recording a write. Bare
   `(x): T` contributes neither a binding nor a name use. Count
   `(x): T = value` as an assignment for declaration ordering. Check the
   unbound local error with a same-named global and both orders of a
   parenthesized assignment and `global`. Cover `nonlocal` in feature commit 6.
2. **Refactor the existing scope analyzer around one compilation.** Separate
   binding collection from slot assignment. Share one set of load, store, and
   delete annotations indexed by AST node ID. Keep bindings and named-slot
   layout per scope and run flow analysis per body. Analyze default expressions
   in the enclosing scope. Preserve current binding and lowering behavior.
   Verify nested defaults and linear annotation storage with many sibling
   functions.
3. **Model the incoming accumulator in JIT state.** Use a tagged entry
   parameter with fixed incoming location `x20` and recover it through existing
   snapshots. Declare the accumulator as an unconditional input and the result
   destination for `CallCodeObject`. Verify forced side exits before and after
   an accumulator overwrite, and preserve the existing Python argument
   locations. This stage can proceed independently of stages 1 and 2.
4. **Carry the selected function through ordinary call entry.** After
   adaptation, put the exact resolved `Function` in the accumulator for
   interpreter or JIT entry. Expanded calls use the function retained across
   adaptation. Use the accumulator-preserving helper at the existing
   interpreter entry poll. Check positional, keyword, expanded, method, and
   native-to-managed calls, force the existing entry safepoint, and run the
   Release opcode-frame check.

Land the preparation before enabling closures in source programs. The
annotation fix and analyzer refactor should be reviewed in that order. The
entry work can be reviewed separately, with stage 3 establishing JIT recovery
before stage 4 supplies the function.

## Feature Commits

1. **Cell and closure storage.** Add `Cell : Object` with a traced
   `Member<Value>`, the optional closure tuple on `Function`, and the
   free-variable count and name metadata on `CodeObject`. Verify that the
   native layouts trace the new strong references and construction enforces
   the expected closure count.
2. **Cell operations.** Add `MakeCell`, `LoadCellChecked`, `StoreCell`, and
   `DelCell` with bytecode metadata and interpreter handlers. A bytecode
   fixture should distinguish boxing a bound parameter from boxing an empty
   local, and exercise both empty-cell error classes.
3. **Closure creation and entry.** Add `CopyFreeVars`,
   `CreateFunctionWithClosure`, and `CreateFunctionWithDefaultsAndClosure`.
   Execute a bytecode fixture with two functions sharing one code object and
   capturing different cells. Force an existing JIT side exit at
   `CopyFreeVars` and verify that each invocation recovers its own function.
4. **Constructor thunk entry.** Put the exact target function in the thunk's
   constant table and emit `LdaConstant target_function` immediately before
   `CallCodeObject` only when the target has free variables. Exercise a
   closure-backed `__init__` or `__new__` with already-bound arguments.
   Verify that a closure-free target keeps its existing instruction sequence.
5. **Enable lexical captures.** Resolve `Cell` and `Free` in the unified
   analyzer, assign the free-variable locals prefix, and emit closure bytecode.
   Include capture forwarding and explicit rejection across class bodies.
   Audit lowering paths that consume or write frame slots directly, especially
   assignment, loop targets, and exception targets and their cleanup. Verify
   sibling sharing, separate activations, late binding, shadowing, recursive
   closures, defaults, and a capture of an annotation-only local.
6. **Enable `nonlocal`.** Parse the directive and extend shared declaration
   validation and resolution. Lower reads, writes, and deletion through the
   enclosing cell. Verify write-only capture, rebinding visible to a sibling,
   deletion, missing enclosing bindings, and declaration ordering with
   simple and parenthesized annotations.

Commits 1 through 4 exercise the runtime using bytecode fixtures before
commit 5 exposes captures through source compilation. Keep classification
and lowering together so every binding classified as a cell receives the
correct slot layout and access operations.

Use focused checks at each checkpoint and the repository's Debug and Release
checks at their required gates. The agreed first-slice scope defers class-body
capture and Python-visible introspection. The general register-encoding fix,
GC scan extent, and automatic JIT tiering policy remain separate work.
