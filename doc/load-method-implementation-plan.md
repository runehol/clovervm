# LoadMethod Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Proposed |
| Implementation | Not started |
| Scope | Staged delivery of early attribute lookup and receiver-aware direct calls |
| Owning layers | Bytecode encoding and decoding, interpreter dispatch, call adaptation, codegen, and JIT fallback |
| Validated against | N/A |
| Supersedes | N/A |

The [direct method-call design](call-argument-temporaries-and-lazy-tracebacks.md#direct-method-calls)
defines the register pair and managed getter return. The
[descriptor lookup design](descriptor-execution.md#direct-method-lookup-design)
defines binding and cache eligibility.

Build the load first. Hand-built bytecode can exercise its outputs before
there are receiver-aware call instructions. Keep compiler emission on the
existing path through the first three slices, then switch all three call forms
together so source calls acquire the same lookup timing in one change.

## 1. Load the method and finish managed getter returns

Add `LoadMethod` and `FinishLoadMethod S` to the builder, decoder, printer, and
interpreter. Decode the pair as one semantic load and support standalone
decoding at the continuation PC. Reuse guarded attribute lookup and the
managed getter call path. Direct lookup writes the pair and skips the
continuation. A getter returns to the continuation, which commits the result
to `S` and the absent receiver to `S+1`.

Use hand-built bytecode to inspect the pair for direct binding, absent
receivers, and inherited classmethods. Check base aliasing with `S+1`, the
compound instruction length, and the standalone continuation's operands.
Exercise getter exceptions and recursive entry. Force a safepoint at getter
return and verify that the continuation sees the rooted accumulator and
commits the pair once. Check that the initial JIT fallback resumes at the
start of the load with the base available.

## 2. Add positional and keyword method calls

Add `CallMethodPositional` and `CallMethodKeyword` together. Both consume the
pair at `S` and use `S+1` as the aligned start of the outgoing window. Reuse
the existing positional and keyword adaptation paths and their call ICs.

Exercise both instructions with hand-built pairs and with `LoadMethod`
output. Cover a present receiver, no-receiver compaction, zero positional
arguments, and keyword values staged outside the outgoing window. At one
site, vary whether lookup supplies a receiver. Also cover a saved callable
whose own `__call__` supplies the only receiver. Check that the physical span
and the adapter's positional count agree on each path.

## 3. Add expanded method calls

Add `CallMethodExpanded` using the existing tuple-plus-prefix binder. Keep
the pair and both transport registers outside the outgoing window. Reuse the
existing cache guards and keep the selected function and prefix live across
callback-capable binding.

Verify reentry, including a nested call that replaces the site's live cache.
Check that supplying a receiver uses the positional view without allocating a
prefix tuple, and that a callee collector still packs its suffix when needed.
Verify that the interpreter fallback snapshot makes the pair and the tuple
and dict transport available at the call PC.

## 4. Switch source lowering and retire fused calls

Emit the load before argument evaluation for positional, keyword, and expanded
attribute calls. Reserve the fixed-call pair and positional span together,
with any padding before the pair. Reserve expanded transport and its
separate outgoing anchor. Check signed register-operand limits with the new
layout. If a previously encodable call exceeds the limit, bring the encoding
or layout choice back to review. Keep plain calls on their existing opcodes.

Replace late-lookup expectations with source-level tests across all three
forms. A missing attribute or a getter failure prevents argument evaluation.
Method replacement during an argument or expansion leaves the saved target in
use. Generic callability is checked after argument construction. Verify the
existing expansion order, including lone-star tuple conversion, and use the
saved callable as keyword construction error context. Remove the fused
emitters and handlers after their last users have migrated, then update the
architecture docs to describe the shipped behavior.

## Verification and scope

Pin the fused benchmark baseline before the first code slice. Run
`ninja -C build-debug all check` for each slice. As hot handlers land, update
the required-handler list and inspect Release `check_opcode_frames` diagnostics.
After the codegen switch, verify actual JIT handoff on AArch64 and run the
matched JIT-off and JIT-on Release comparison under the repository's shared
build and timing lock. Use the workloads in the
[design's verification section](call-argument-temporaries-and-lazy-tracebacks.md#lifetimes-and-verification).
The quantitative regression budget remains proposed.

The first implementation review covers slice 1. Native-origin descriptor
callbacks, arbitrary callable descriptor hooks, the callee-owned frame
convention, and the dependent stdlib work have separate scopes.
