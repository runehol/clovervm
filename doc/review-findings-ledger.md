# Review Findings Ledger

This ledger is the durable record for the repository-wide correctness campaign
defined in the [Comprehensive Review Plan](comprehensive-review-plan.md).

Only reachable, high-confidence defects belong under confirmed findings.
Suspicions that still require proof belong in the investigation queue. Missing
features and intentional Python deviations belong in their existing planning or
deviation documents unless their implementation is itself incorrect.

## Ledger Rules

- Give every entry a stable ID of the form `CVR-NNN`.
- Never reuse an ID, including after an entry is rejected or superseded.
- Record the exact reviewed commit or range.
- Link exact source and test locations using repository-relative paths.
- State reachability and observable impact, not just the suspicious code shape.
- Record attempts to disprove the issue.
- Keep the original finding after resolution; append its disposition and fix.
- Do not silently promote an investigation into a finding. Rewrite it against
  the confirmed-finding template and link the original investigation.

Finding status is one of:

- `open`: confirmed and not yet fixed;
- `fix in progress`: an authorized fix is being developed;
- `resolved`: the fix and regression coverage have landed;
- `accepted`: confirmed behavior intentionally remains;
- `rejected`: reviewed evidence is retained, but the project does not classify
  the entry as a defect;
- `superseded`: replaced by another finding with a more accurate boundary.

Investigation status is one of:

- `untriaged`: recorded but not yet examined deeply;
- `investigating`: active evidence gathering;
- `promoted`: established as a confirmed finding;
- `rejected`: disproved or covered by an existing invariant;
- `deferred`: credible but blocked on a named prerequisite.

## Campaign Baseline

- Starting commit: `8cf6c1c`
- Branch: `main`
- Worktree at baseline: clean
- Baseline verification: `ninja -C build-debug all check`
- Result: 1,237 tests passed in 35 suites; one test disabled

## Confirmed Findings

### CVR-002: Child imports mask missing transitive dependencies

- Severity: P1
- Status: open
- Review unit: R2
- Found at: `e7bd814`
- Affected code: `src/import_system/import_system.cpp:659`
- Affected tests: none

Invariant or semantic rule:

`from package import child` may translate a `ModuleNotFoundError` into a missing
attribute `ImportError` only when the requested child itself was not found. An
exception raised while executing an existing child module must propagate.

Reachable path:

When `package.child` is not already an attribute, `import_from()` imports that
module. If the import returns any exact `ModuleNotFoundError`, lines 659-674
clear it and synthesize `cannot import name`, without checking which module was
missing.

Observable impact:

An existing child containing `import missing_dependency` reports that the child
cannot be imported rather than reporting the missing dependency. This changes
the exception type/message boundary seen by callers and hides the actionable
cause.

Evidence and reproduction:

A temporary `pkg_r2/child.py` containing
`import dependency_that_does_not_exist_r2` was imported with
`from pkg_r2 import child`. CloverVM produced
`ImportError: cannot import name 'child' from 'pkg_r2'`; CPython preserved
`ModuleNotFoundError: No module named 'dependency_that_does_not_exist_r2'`.

Disproof attempts:

The audit checked for stored missing-module identity on the current exception.
The minimal exception object exposes no equivalent of CPython's
`ModuleNotFoundError.name`, and this path compares only the exception class.
Existing import tests cover a genuinely absent child but not failure inside an
existing child.

Recommended fix boundary:

Preserve the original exception unless the import machinery can prove that the
requested child module itself was absent. Add separate tests for absent child
and missing transitive dependency.

Verification:

Confirmed with CloverVM and CPython command-line reproductions. No fix has been
implemented.

Disposition:

Open.

### CVR-009: Large right shifts execute undefined native shifts

- Severity: P1
- Status: open
- Review unit: R4
- Found at: `af8b450`
- Affected code: `src/runtime/interpreter.cpp:3556`,
  `src/runtime/interpreter.cpp:3576`, `src/compiler/codegen.cpp:288`
- Affected tests: none

Invariant or semantic rule:

Python right shift accepts arbitrary nonnegative counts. Counts at least the
native word width must saturate an SMI result to `0` for nonnegative values or
`-1` for negative values; they must never reach a C++ shift expression.

Reachable path:

Both register and immediate SMI handlers execute `a.as.integer >> shift_count`
without guarding counts of 64 or greater. Codegen restricts large immediate
left shifts but applies no corresponding right-shift restriction, so constants
reach `RShiftSmi`; variable counts reach `RShift`.

Observable impact:

The shift is C++ undefined behavior. On the current arm64 debug and release
builds, the hardware masks the count: `1 >> 64` and `1 >> 128` both produce `1`
instead of Python's `0`.

Evidence and reproduction:

`build-debug/src/clovervm -c 'assert 1 >> 64 == 0'` fails, as does the release
binary. CPython satisfies the assertion. Inspection confirms that both handlers
perform the unchecked shift.

Disproof attempts:

The slow dispatch path runs only for non-SMI operands. Shift counts are full
SMIs in the generic handler, and the immediate selector accepts signed-byte
constants including 64 through 127.

Recommended fix boundary:

Handle counts at least 64 before the native shift in both handlers, preserving
negative-count `ValueError`, and add constant and variable-count tests for
positive and negative operands around 63, 64, and larger counts.

Verification:

Confirmed in debug and release against CPython. No fix has been implemented.

Disposition:

Open.

### CVR-011: Register encoding wraps and crashes large functions

- Severity: P1
- Status: open
- Review unit: R4
- Found at: `af8b450`
- Affected code: `src/bytecode/code_object.h:592`,
  `src/bytecode/code_object_builder.cpp:1161`
- Affected tests: none

Invariant or semantic rule:

Every semantic register emitted into a signed-byte operand must be representable
in that operand and decode back to the same frame slot. Unsupported frame sizes
must be rejected before bytecode emission rather than silently narrowed.

Reachable path:

`CodeObject::encode_reg()` returns an `int8_t` from unchecked unsigned register
arithmetic. Builder emitters and finalization store the narrowed result. A
function with 127 named locals places its last semantic register outside the
signed-byte reach and wraps it into a positive frame offset.

Observable impact:

The interpreter accesses the wrong frame location and crashes with signal 11 in
both debug and release builds. This is reachable from ordinary valid Python
source without unusual runtime state.

Evidence and reproduction:

A generated function assigning `v0` through `v126` and returning
`(v0, v126)` exits with status 139 in both CloverVM builds. The corresponding
126-local function succeeds.

Disproof attempts:

No parser, scope, codegen, builder, or finalization limit rejects the frame
before narrowing. Debug `decode_reg()` assertions operate only after the encoded
operand has already lost its identity. Large-call argument counts have related
unchecked `uint8_t` narrowing but currently encounter this register-window
failure first.

Recommended fix boundary:

Establish an explicit representability check or widen the register encoding
across builder, bytecode, and interpreter layers. Reject unsupported frames with
a deliberate compilation error. Audit argument-count operands when settling
the chosen width.

Verification:

Confirmed with independent generated-source runs in debug and release. No fix
has been implemented.

Disposition:

Open.

### CVR-001: Slab allocation advances beyond its mapped extent

- Severity: P2
- Status: open
- Review unit: R1
- Found at: `c289490`
- Affected code: `src/memory/slab_allocator.h:119`
- Affected tests: none

Invariant or semantic rule:

A bump allocator must validate the aligned allocation extent before forming its
next cursor. It must not form a pointer outside the allocation or overflow the
rounding arithmetic.

Reachable path:

`SlabAllocator::allocate()` compares `curr_ptr + n_bytes` with `end_ptr`, then
rounds `n_bytes` up to the 32-byte value granularity and advances by the larger
amount. A refcounted ordinary slab starts at offset 16, leaving a usable extent
that is 16 modulo 32. A small final request can therefore fit before rounding
but advance the cursor 16 bytes past the slab. Dedicated allocations reproduce
the same condition whenever their raw requested size is not 32-byte aligned,
because `GlobalHeap::allocate_large_object()` sizes the slab for the unrounded
payload plus the pointer-tag offset.

Observable impact:

Forming the out-of-range cursor is C++ undefined behavior. A later allocation
also evaluates pointer arithmetic from that invalid cursor. The original
payload can remain within mapped memory, so the defect need not produce an
immediate out-of-bounds write or sanitizer report.

Evidence and reproduction:

At `src/memory/slab_allocator.h:121`, the bounds check uses the original
`n_bytes`; lines 126-128 round and advance afterward. For an ordinary
refcounted slab, `65536 - 16 == 65520`, leaving a final 16-byte remainder after
32-byte bumps. For a dedicated allocation of `LargeAllocationSize + 8`, the
raw payload fits exactly but the rounded bump exceeds the constructed slab
extent.

Disproof attempts:

The audit checked whether callers pre-align sizes. They do not: allocation size
is the concrete `sizeof(T)` or dynamic `T::size_for(...)`, and the slab
allocator owns alignment. Dedicated slab sizing also uses the unrounded size.
Existing debug, release, and focused sanitizer tests pass because they validate
payload behavior, not the internal cursor value.

Recommended fix boundary:

Perform checked rounding before the bounds comparison, avoid pointer addition
until the rounded size is known to fit, and compare against the remaining byte
count. Add ordinary-end and non-aligned dedicated-allocation regression tests.

Verification:

The finding is established by allocator arithmetic and reachable slab layouts.
No fix has been implemented or verified yet.

Disposition:

Open pending authorization to implement the focused allocator fix and tests.

### CVR-003: Re-raising an exception creates a self context

- Severity: P2
- Status: open
- Review unit: R2
- Found at: `e7bd814`
- Affected code: `src/runtime/interpreter.cpp:860`
- Affected tests: none

Invariant or semantic rule:

Automatic exception chaining must not make an exception its own `__context__`
or introduce a context cycle.

Reachable path:

`raise error` inside the handler for that same `error` reaches
`set_exception_context()`, which unconditionally stores the active exception as
the raised exception's context.

Observable impact:

CloverVM makes `reraised.__context__ is reraised` true and creates a
Python-visible ownership cycle. CPython leaves the self context unset.

Evidence and reproduction:

A nested handler that catches `ValueError as error`, executes `raise error`,
then checks the caught value's context succeeds on
`assert reraised.__context__ is reraised` in CloverVM. The same assertion fails
in CPython.

Disproof attempts:

No identity or existing-chain guard runs before the context store. Handling
only direct identity would prove this reproduction but would not address longer
cycles through an existing context chain.

Recommended fix boundary:

Centralize cycle-safe automatic context assignment and add direct self-cycle
and longer context-chain tests.

Verification:

Confirmed with CloverVM and CPython command-line reproductions. No fix has been
implemented.

Disposition:

Open.

### CVR-004: Handler matching discards exception context

- Severity: P2
- Status: open
- Review unit: R2
- Found at: `e7bd814`
- Affected code: `src/runtime/interpreter.cpp:143`,
  `src/runtime/interpreter.cpp:247`, `src/runtime/interpreter.cpp:737`
- Affected tests: none

Invariant or semantic rule:

An exception raised while handling or matching another active exception must
chain the displaced exception as its context.

Reachable path:

An invalid target such as `except 1` reaches
`exception_handler_type_error()`. The shared setter installs a new pending
`TypeError` directly, overwriting the active exception without assigning it as
context.

Observable impact:

CPython exposes the original `ValueError` as the resulting `TypeError`'s
`__context__`. CloverVM loses it; accessing the missing context currently raises
`AttributeError`.

Evidence and reproduction:

An outer handler around `try: raise ValueError; except 1: pass` catches the
generated `TypeError`. CPython retains the `ValueError` context, while CloverVM
does not.

Disproof attempts:

The exceptional unwind retains pending state, but the setter replaces that
state before unwind resolution. No caller saves or restores the displaced
exception.

Recommended fix boundary:

Route exceptions raised during handler matching through the same cycle-safe
automatic chaining policy used by ordinary `raise` processing.

Verification:

Confirmed with focused CloverVM and CPython reproductions. No fix has been
implemented.

Disposition:

Open.

### CVR-005: Native callbacks can return normally with pending exceptions

- Severity: P2
- Status: open
- Review unit: R2
- Found at: `e7bd814`
- Affected code: `src/runtime/interpreter.cpp:5481`
- Affected tests: none

Invariant or semantic rule:

A native callback success must return a normal handle with no pending
exception. A pending exception must be paired with the extension error marker.

Reachable path:

All `op_call_extension0` through `op_call_extension7` handlers unwrap the
returned handle and complete normally without checking pending exception state.
An extension can call a failing C API helper, ignore its error status, and
return `clover_none(ctx)`.

Observable impact:

The Python call appears successful while a latent exception remains on
`ThreadState`. It may be overwritten, cleared, or surface at an unrelated later
boundary. The reverse mismatch, error marker without pending state, is already
detected as `SystemError`.

Evidence and reproduction:

The callback opcodes contain no pending-state validation after the extension
function returns. The documented C API contract requires normal return with no
exception or error-marker return with pending state; current code validates only
the latter when the managed adapter observes a marker.

Disproof attempts:

C API helpers do not force control flow after setting an exception, and the
opaque context remains usable by extension code. Therefore a callback that
ignores status can reach the normal return. No later opcode checks pending state
on ordinary completion.

Recommended fix boundary:

Validate both halves of the callback result/pending-state contract in the
shared extension-call path and raise `SystemError` for mismatches. Add a test
extension callback that returns normally after a helper failure.

Verification:

Established from the documented extension contract and complete opcode paths.
No fix has been implemented.

Disposition:

Open.

### CVR-006: Native module initialization accepts success with an exception

- Severity: P2
- Status: open
- Review unit: R2
- Found at: `e7bd814`
- Affected code: `src/native/native_module_loader.cpp:162`
- Affected tests: none

Invariant or semantic rule:

`CLOVER_STATUS_OK` from a module initializer must correspond to no pending
exception; `CLOVER_STATUS_ERROR` must correspond to pending exception state.

Reachable path:

`exec_native_extension_module()` immediately accepts `CLOVER_STATUS_OK` and
returns `None` without inspecting the thread's pending state. An initializer can
call a failing builder or conversion helper, ignore the result, and return OK.

Observable impact:

The module is treated as successfully initialized and can remain cached while
the thread carries an unrelated pending exception. The exception may later be
overwritten or escape at the wrong boundary.

Evidence and reproduction:

Lines 162-171 validate `CLOVER_STATUS_ERROR` without an exception but have no
symmetric success-with-exception check. The C API permits helpers to establish
pending state before the initializer chooses its return status.

Disproof attempts:

The builder API reports failures through status rather than forcing an early
return. Module initialization is external C code, so the loader cannot assume
the status was observed.

Recommended fix boundary:

Validate the complete status/pending-state matrix at the loader boundary and
fail module initialization with `SystemError` for inconsistent success. Add a
native test module that returns OK after establishing an exception.

Verification:

Established from the documented initializer contract and loader control flow.
No fix has been implemented.

Disposition:

Open.

### CVR-007: Dictionary iterators revive after invalidation

- Severity: P2
- Status: open
- Review unit: R3
- Found at: `b9ed3d9`
- Affected code: `src/builtin_types/dict_view.cpp:99`,
  `src/builtin_types/dict_view.h:79`
- Affected tests: none

Invariant or semantic rule:

Once a dictionary iterator observes an invalidating size change and raises
`RuntimeError`, it must remain invalid. Restoring the dictionary's original size
must not make iteration resume.

Reachable path:

All three view iterator `__next__` implementations call
`check_expected_size()`, which compares the current length with the original
length but stores no invalid state. After a mismatch raises, deleting or adding
entries until the original length is restored makes the next comparison pass.

Observable impact:

An iterator that already reported `dictionary changed size during iteration`
can later yield keys, values, or items from the mutated dictionary. CPython
continues raising `RuntimeError` from the poisoned iterator.

Evidence and reproduction:

Create `{'a': 1, 'b': 2}`, advance its keys-view iterator once, insert `'c'`,
and confirm that the next call raises `RuntimeError`. Then delete `'c'` and call
`__next__` again. CloverVM resumes and yields `'b'`; CPython raises
`RuntimeError` again. The iterator records only `dict`, `index`, and
`expected_size`.

Disproof attempts:

The audit checked for invalidation in the error path, iterator object shape, and
dictionary generation metadata. The error helper receives only the dictionary
and expected size, and no iterator field is changed. Table generation is not
stored on the iterator.

Recommended fix boundary:

Add an explicit poisoned state shared by key, value, and item iterators, set it
before returning the first mutation error, and keep returning `RuntimeError` on
later calls. Preserve the separately documented length-only detection policy
for same-size mutations unless that design is intentionally revisited.

Verification:

Confirmed with direct CloverVM and CPython command-line reproductions. No fix
has been implemented.

Disposition:

Open.

### CVR-008: Opcode-frame verification cannot fail its build target

- Severity: P2
- Status: rejected
- Review unit: R4
- Found at: `af8b450`
- Affected code: `benchmark/CMakeLists.txt:80`,
  `tools/check_opcode_frames.py:157`
- Affected tests: none

Invariant or semantic rule:

The required release opcode-frame verification gate must fail when a listed hot
handler is missing, cannot be inspected, or gains stack-frame setup.

Reachable path:

The `check_opcode_frames` CMake target always passes `--warn-only`. In that mode
the checker deliberately converts missing binaries, missing required handlers,
tool failures, and detected frames into a successful exit status.
`run_benchmark` depends on the same non-enforcing target.

Observable impact:

The documented verification command remains green after the exact regressions
it is intended to prevent. This allows release-only handler-shape regressions to
land without failing the build or benchmark gate.

Evidence and reproduction:

The CMake command line contains `--warn-only`, and every checker error return is
conditionalized to success in that mode. A direct strict invocation against the
current release object succeeds and reports 98 required handlers out of 229
discovered, confirming that current handlers pass while the target itself does
not enforce future results.

Disproof attempts:

No enclosing CMake command parses warning output or converts it back to failure.
The custom target and `run_benchmark` rely only on the script exit status.

Recommended fix boundary:

Remove `--warn-only` from the required verification target, or split explicit
enforcing and diagnostic targets. Keep platform-specific diagnostic policy out
of the gate used by repository checks.

Verification:

The ordinary CMake target succeeded in warning mode; the equivalent strict
checker invocation passed the current tree. No fix has been implemented.

Disposition:

Rejected by project decision. Warning-only frame reporting is not treated as a
correctness defect or required failing gate.

### CVR-010: Packed jump operands use unaligned typed reads

- Severity: P2
- Status: rejected
- Review unit: R4
- Found at: `af8b450`
- Affected code: `src/runtime/interpreter.cpp:105`
- Affected tests: none

Invariant or semantic rule:

Packed bytecode operands must be decoded without assuming typed alignment or an
`int16_t` object at the byte address.

Reachable path:

Ordinary builds cast arbitrary bytecode addresses to `const int16_t *` and
dereference them. Relative jump operands begin at offsets such as `pc + 1` and
`pc + 2`, so alignment is not guaranteed. Only builds defining
`NDEBUG_SANITIZER` use safe byte assembly.

Observable impact:

The typed dereference is C++ undefined behavior and is unsafe on strict-alignment
targets or under optimizer assumptions. Current arm64 hardware tolerates the
access, while the sanitizer-specific safe path prevents sanitizer builds from
exercising the ordinary implementation.

Evidence and reproduction:

All relative jump handlers use `read_int16_le()` on packed operands. Lines
107-113 select bytewise decoding only for the sanitizer macro and otherwise
perform the unaligned typed load.

Disproof attempts:

Bytecode storage is byte-aligned, and instruction positions/operand offsets vary;
there is no builder alignment invariant for every jump operand. Hardware support
for unaligned loads does not make the C++ expression defined.

Recommended fix boundary:

Use the bytewise decoder in every build, or an equivalent `memcpy`-based load
with explicit little-endian conversion. Remove the sanitizer-only semantic
difference.

Verification:

Confirmed by packed-layout and C++ alignment analysis. No current-hardware
failure was required or observed, and no fix has been implemented.

Disposition:

Rejected by project decision. The typed load is intentionally isolated in the
target-specific `read_int16_le()` primitive, which is designed to be replaced
on targets that cannot safely support it.

Add findings in descending severity, then ascending ID. Use this template:

```md
### CVR-NNN: Short imperative-free summary

- Severity: P0 | P1 | P2 | P3
- Status: open
- Review unit: R1-R10
- Found at: commit or range
- Affected code: `path:line`
- Affected tests: `path:line`, or none

Invariant or semantic rule:

Reachable path:

Observable impact:

Evidence and reproduction:

Disproof attempts:

Recommended fix boundary:

Verification:

Disposition:
```

## Investigation Queue

No open investigations have been recorded yet.

An investigation is not a finding. Use this template:

```md
### CVR-NNN: Question being investigated

- Status: untriaged
- Review unit: R1-R10
- Raised at: commit or range
- Relevant code: `path:line`

Suspected risk:

Evidence needed:

Known guards or caller proofs:

Next check:

Disposition:
```

## Review Unit Log

Record completed review units even when they produce no findings. This prevents
later readers from interpreting an empty findings list as evidence that a
surface was reviewed.

Use this template:

```md
### Rn: Review unit name at commit-or-range

- Scope:
- Design documents read:
- Code and tests reviewed:
- Confirmed findings: none | CVR-NNN, ...
- Investigations: none | CVR-NNN, ...
- Verification commands:
- Verification result:
- Unreviewed edges:
- Residual risk:
```

### R1: Memory, Ownership, And Reclamation At `c289490`

- Scope: all 33 registered native layouts and their release/object-size
  descriptors; custom teardown; heap construction; refcount and lifecycle
  transitions; ZCT and epoch discovery; slab pins, bitmaps, allocation, and
  reuse; managed root collection; and native/managed re-entry publication.
- Design documents read: `refcounting-and-reclamation.md`,
  `heap-object-metadata.md`, `native-layout-descriptors.md`,
  `typed-handles-and-expected.md`, `native-managed-boundaries.md`, and
  `heap-slab-allocation-and-reuse.md`.
- Code and tests reviewed: `src/memory`, native layout declarations and
  registry, every registered heap record, `Shape` and `CodeObject` custom
  teardown, thread-state call adapters, interpreter native and import re-entry
  paths, and the focused heap, reclamation, layout, dictionary, import, and
  safepoint tests.
- Confirmed findings: CVR-001.
- Investigations: none.
- Verification commands: `ninja -C build-debug all check` at the campaign
  baseline; focused debug and release test filters; the R1-focused ASan+UBSan
  filter with the diagnostic macOS container-overflow workaround; and
  `cmake --build build-release --target check_opcode_frames`.
- Verification result: the baseline and final debug gates each passed 1,237
  tests. The consolidated release R1 filter passed 83 tests, the diagnostic
  ASan+UBSan R1 filter passed 65 tests, and the re-entry filter passed 69 tests.
  The opcode frame checker passed. The full macOS sanitizer binary still aborts
  during GoogleTest static registration with container-overflow checking
  enabled, before CloverVM tests run.
- Unreviewed edges: multithreaded refcount and lifecycle atomics, cycle
  collection, moving-GC barriers, and future JIT root maps are designs rather
  than implemented R1 surfaces and were not reviewed as current behavior.
- Residual risk: static release-span contiguity and custom teardown completeness
  are convention-based rather than mechanically tied to fields. Dynamic count,
  lifecycle, pin, and bitmap corruption is guarded primarily by debug asserts,
  though no reachable invariant break was found. `clover_frame_frontier`
  currently carries both a frame-chain anchor concept and, for arbitrary-frame
  re-entry, a lowest-live-slot scan boundary; the current contiguous scanner is
  safe, but future frame-chain or JIT work must separate those concepts.

### R2: Pending Exceptions And Fallible Boundaries At `e7bd814`

- Scope: `Expected<T>` and raw-marker propagation, pending-state setters and
  clearing, interpreter unwind and continuation paths, native callbacks,
  extension APIs, module initialization, import cleanup, and exception
  replacement/chaining.
- Design documents read: `exception-transport-and-protocols.md`,
  `typed-handles-and-expected.md`, `native-managed-boundaries.md`,
  `clover-c-api.md`, and `native-c-modules.md`.
- Code and tests reviewed: all `set_pending_*`, `clear_pending_exception`,
  `CL_TRY`, `CL_PROPAGATE_EXCEPTION`, `CL_SWALLOW_EXCEPTION`, discarded
  fallible-result, and exception-marker sites; interpreter exceptional returns
  and handler matching; attribute mutation; import paths; the extension API and
  native loader; and focused exception, typed-value, attribute, import, and
  native-module tests.
- Confirmed findings: CVR-002, CVR-003, CVR-004, CVR-005, CVR-006.
- Investigations: none.
- Verification commands: focused debug exception/attribute/typed-value tests;
  focused import and native-module tests; direct CloverVM and CPython
  reproductions for CVR-002 through CVR-004; release tests for the reviewed
  surfaces; and `ninja -C build-debug all check`.
- Verification result: the focused debug and release filters each passed 90
  tests. The final debug gate passed all 1,237 enabled tests; one test remains
  disabled. The direct differential probes reproduced CVR-002 through CVR-004.
- Unreviewed edges: future lazy traceback state and stop-returning generator
  protocols are designs rather than implemented R2 surfaces.
- Residual risk: attribute mutation still uses a dual-channel `bool` plus
  pending-exception contract, though all current production callers check the
  pending state before synthesizing another error. Static proof that every raw
  `Value` caller recognizes `exception_marker()` remains weaker than the typed
  `Expected<T>` path. Some older tuple/string/numeric extension helpers do not
  reject error-marker input consistently; current C API rules require extension
  code to propagate such a handle immediately, so this was not split into a
  separate finding.

### R3: Dictionaries, Hashing, And Equality At `b9ed3d9`

- Scope: canonical exact-string and general dictionary shapes, promotion,
  trusted dunder handlers, generated method bytecode, semantic C++ and C APIs,
  hashing and equality callbacks, probe restart and mutation revalidation,
  insertion order, bulk operations, views, and iterators.
- Design documents read: `dictionaries.md`, `iteration-plans.md`, and the
  dictionary sections of `clover-c-api.md`.
- Code and tests reviewed: `dict.cpp`, `dict.h`, `dict_view.cpp`,
  `dict_view.h`, dictionary bytecodes and trusted resolver paths, hash
  canonicalization, `ThreadState` hash/equality helpers, import-system
  `sys.modules` operations, and focused dictionary/hash/import tests.
- Confirmed findings: CVR-007.
- Investigations: none.
- Verification commands: focused debug and release
  `Dict.*:Hash.*:ImportSystem.*` filters; direct CloverVM and CPython iterator
  probes; release opcode-frame checking; and `ninja -C build-debug all check`.
- Verification result: the focused debug and release filters each passed 174
  tests. The iterator poisoning differential probe reproduced CVR-007. The
  release opcode-frame checker passed, and the final debug gate passed all
  1,237 enabled tests; one test remains disabled.
- Unreviewed edges: general dictionary subclass construction, future classmethod
  descriptor behavior, and general iteration-plan integration are not currently
  implemented surfaces.
- Residual risk: static generated-bytecode/register invariants are not
  mechanically derived from the dict probe contract. Table generation can
  theoretically overflow its SMI representation but is unreachable at
  practical operation counts. Exact-string assumptions must be revisited if
  constructible `str` subclasses make overridden hash/equality behavior
  reachable. The iteration design intentionally detects length changes rather
  than every same-size key-set mutation; `dict.fromkeys` tuple/list restriction
  and non-classmethod binding are also documented current limitations rather
  than new findings.

### R4: Bytecode And Interpreter Integrity At `af8b450`

- Scope: bytecode enum and formatting, builder emitters and relocations,
  codegen selection, operand widths, register/frame encoding, dispatch-table
  coverage, handler instruction lengths, jumps and exception continuations,
  release `musttail` shape, and the hot-handler frame gate.
- Design documents read: `python-opcode-design-notes.md`,
  `function-calling-convention.md`, `inline-cache-slot-layout.md`, and the
  interpreter sections of `architecture.md`.
- Code and tests reviewed: `src/bytecode`, codegen bytecode emission,
  `interpreter.cpp`, opcode printing/disassembly, exception-table relocation and
  lookup, call-window setup, `tools/check_opcode_frames.py`, the required hot
  handler list, and focused codegen/interpreter tests.
- Confirmed findings: CVR-009, CVR-011.
- Rejected entries: CVR-008, CVR-010.
- Investigations: none.
- Verification commands: enum/dispatch/formatter consistency scans;
  `Codegen.*:Interpreter.*` tests; direct debug and release reproductions for
  large right shifts and 127-local functions; CMake and strict opcode-frame
  checker invocations; release focused tests; and
  `ninja -C build-debug all check`.
- Verification result: focused debug and release filters each passed 562 tests.
  The strict frame checker passed all 98 required handlers out of 229 discovered
  opcode symbols. Debug and release reproductions confirmed CVR-009 and
  CVR-011. The final debug gate passed all 1,237 enabled tests; one test remains
  disabled.
- Unreviewed edges: JIT bytecode entry/deoptimization and compiled exception
  targets are designs rather than implemented R4 surfaces.
- Residual risk: opcode definitions are duplicated across enum, builders,
  dispatch, handlers, and printers rather than generated from one schema.
  `Bytecode::Nop` has no handler but also has no emitter or caller. Signed
  16-bit jump relocations reject very large valid function bodies with a
  controlled `SystemError`; this is an undocumented bytecode capacity limit,
  not a memory-safety failure. Argument counts also narrow to `uint8_t`, though
  the current signed-byte register limit fails first. The frame checker covers
  98 of 229 discovered `op_*` symbols; the unlisted set mixes deliberate cold
  helpers with handlers whose performance policy has not been classified.
  Packed 16-bit reads remain isolated in a target-specific primitive that must
  be replaced when bringing CloverVM to a target without suitable unaligned
  access support.

## Resolved And Rejected Index

Keep a compact index here after entries acquire final dispositions:

| ID | Type | Final status | Resolution |
| --- | --- | --- | --- |
| CVR-008 | finding | rejected | Warning-only frame reporting is intentional policy. |
| CVR-010 | finding | rejected | Packed reads are an isolated target-specific primitive. |
