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
decoding at the continuation PC. Share the `LoadAttr` resolver, cache
eligibility, read plans, and getter call path. At execution, split binding
for function plans and unwrap exact staticmethod plans. Direct lookup writes
the pair and skips the continuation. A getter returns to the continuation,
which commits the result to `S` and the absent receiver to `S+1`.

Reuse the combined MRO validity cell for cached class-object reads,
including plain-slot reads. It guards shape and contents on the class and
metaclass MROs.
At one warmed load site, replace an inherited plain function with a
`classmethod`, then restore the plain function. Verify invalidation on the
same-shape writes and check the pair after each lookup. Both reads have an
absent receiver. The `classmethod` getter returns a bound method whose
receiver is the accessed derived class. Exercise the shared plain-slot
cache guard by adding `__get__` to a stored value's type after both load
forms have read the slot.

Use hand-built bytecode to inspect the pair for direct binding, absent
receivers, and inherited classmethods. Check base aliasing with `S+1`, the
compound instruction length, and the standalone continuation's operands.
Exercise recursive getter entry and verify that a getter failure reaches the
caller's handler for a protected load. On successful return, assert the exact
`FinishLoadMethod` PC. Force a safepoint there with a fresh heap result rooted
only in the accumulator and verify that the continuation commits the pair
once.

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

## Prerequisite for expanded method calls

Normalize string-subclass names in the existing keyword preparation
opcodes, with duplicate checks on the exact string names. Keep non-string
validation at call entry. Then simplify the expanded binder shared by the
ordinary and fused call opcodes. See
[Function Call Adaptation](function-call-adaptation.md#keyword-construction).

## 3. Add expanded method calls

Add `CallMethodExpanded` using the five-byte expanded-call format and the
ordinary expanded handler. Its first operand names the saved callable and
receiver pair. Keep that pair and both transport registers outside the
separate aligned outgoing window. Use the existing tuple-plus-prefix binder,
which checks keyword storage at call entry and binds without Python callbacks.

Use hand-built bytecode to exercise bound and unbound calls at one site,
callable instances, deferred non-string-key errors, and positional tuple
reuse when only the receiver is consumed before a collector. Keep the
existing reentry coverage in keyword preparation, where managed mappings
can invoke the same call site.

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
After the codegen switch, run the interpreter-only Release comparison under
the repository's shared build and timing lock. Use the workloads in the
[design's verification section](call-argument-temporaries-and-lazy-tracebacks.md#lifetimes-and-verification).
The quantitative regression budget remains proposed.

The bytecode follows the existing interpreter fallback contract. Dedicated
JIT tests and the JIT-on performance comparison are follow-up work.

The first implementation review covers slice 1. Native-origin descriptor
callbacks, arbitrary callable descriptor hooks, the callee-owned frame
convention, and the dependent stdlib work have separate scopes.
