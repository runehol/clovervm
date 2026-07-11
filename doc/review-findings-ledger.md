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
- Status: resolved
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

The internal path loader now returns `not_present` only when discovery proves
that the requested module has no spec. Failures raised while loading an
existing child propagate unchanged, while callers translate a proven miss
according to their own import contract.

Verification:

Added separate coverage for a genuinely absent package child, a missing
transitive dependency, and a child blocked by a `None` `sys.modules` entry.
`ninja -C build-debug all check` passes with 1,248 tests across 36 suites; one
test is disabled.

Disposition:

Resolved locally; uncommitted.

### CVR-009: Large right shifts execute undefined native shifts

- Severity: P1
- Status: resolved
- Review unit: R4
- Found at: `af8b450`
- Affected code: `src/runtime/interpreter.cpp:3556`,
  `src/runtime/interpreter.cpp:3576`, `src/compiler/codegen.cpp:288`
- Affected tests: `tests/test_interpreter.cpp`

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

Added focused immediate and register-count coverage around counts 63, 64, 127,
and 128 for positive and negative operands. The focused interpreter tests and
`ninja -C build-debug all check` pass with 1,239 tests in 35 suites and one
disabled test. The release `check_opcode_frames` target also passes.

Disposition:

Resolved by saturating SMI right shifts before executing the native shift in
both `RShift` and `RShiftSmi`, while preserving the existing negative-count
error path.

### CVR-011: Register encoding wraps and crashes large functions

- Severity: P1
- Status: open
- Review unit: R4
- Found at: `af8b450`
- Affected code: `src/bytecode/code_object.h:592`,
  `src/bytecode/code_object_builder.cpp:1161`,
  `src/bytecode/code_object_builder.cpp:1535`,
  `src/compiler/codegen.cpp:1211`, `src/compiler/codegen.cpp:1284`
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
126-local function succeeds. R6 found the same unchecked register frontier in
call lowering: a generated call with 124 distinct explicit keywords
deterministically crashes the debug compiler/CLI, while CPython prints the
expected result. Calls with 120 keywords succeed; 124 through 130 crash in the
tested shape. Positional and method-keyword lowering share the unchecked
temporary-register reservation, and keyword counts are additionally narrowed
to `uint8_t` after reservation.

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

### CVR-013: Fused method calls resolve attributes after arguments

- Severity: P1
- Status: open
- Review unit: R6
- Found at: `477e1af`
- Affected code: `src/compiler/codegen.cpp:1204`,
  `src/runtime/interpreter.cpp:4760`
- Affected tests: none

Invariant or semantic rule:

Python evaluates the callable expression in a call before evaluating any
argument expression. For `obj.method(arg())`, attribute lookup and binding must
therefore complete before `arg()` runs, and the resolved callable must remain
the target even if argument evaluation mutates `obj` or its class.

Reachable path:

The direct-method lowering saves only the receiver, evaluates every positional
or keyword argument, and then emits `CallMethodAttrPositional` or
`CallMethodAttrKeyword`. The interpreter performs the attribute lookup for the
first time when that fused opcode executes, after all argument side effects.

Observable impact:

Argument evaluation can replace the method and make the call invoke the new
function rather than the one Python already resolved. It can also install an
attribute that was absent when the call expression began, turning a call that
must raise `AttributeError` before evaluating arguments into a successful call.

Evidence and reproduction:

With `C.m` initially returning `1`, an argument function that replaces `C.m`
with a method returning `2` makes `c.m(arg())` return `2` in CloverVM; CPython
returns `1`. With no `c.m` initially present, an argument function that installs
`c.m` makes the CloverVM call succeed, while CPython raises `AttributeError`
without evaluating the argument. The replacement result also reproduces on the
keyword-call path.

Disproof attempts:

Direct method caches correctly reload class slots and adapt binding after
ordinary mutations between calls. The defect is independent of cache warmth:
the callable has not been resolved at all when arguments begin. Non-attribute
call lowering already evaluates and stores the callable before its arguments.

Recommended fix boundary:

Resolve the attribute in call context before evaluating arguments and preserve
the resulting callable plus optional receiver binding across argument
evaluation. Keep descriptor execution and escaped bound-method construction as
their separately documented contracts. Add positional and keyword differential
tests with mutation and missing-attribute side effects.

Verification:

Confirmed with direct CloverVM and CPython reproductions for replacement and
missing-method cases. No fix has been implemented.

Disposition:

Open.

### CVR-014: Recursive calls overrun the fixed Clover stack

- Severity: P1
- Status: open
- Review unit: R6
- Found at: `477e1af`
- Affected code: `src/runtime/thread_state.cpp:66`,
  `src/runtime/interpreter.cpp:1572`
- Affected tests: none

Invariant or semantic rule:

Managed frame entry must never write outside the allocated Clover stack.
Python recursion must fail with `RecursionError` before the physical stack
boundary, respecting the exposed recursion-limit contract where supported.

Reachable path:

Each thread allocates a fixed stack of 1,048,576 `Value` slots. Frame-entry
helpers compute the next lower frame pointer from the call window and callee
layout, then initialize the frame without checking it against
`clover_stack_begin()`. The `sys` recursion-limit API updates metadata but no
call path consults it.

Observable impact:

Unbounded ordinary Python recursion walks off the managed stack and terminates
the process with `SIGSEGV` instead of raising `RecursionError`. This is a
Python-reachable out-of-bounds managed-frame write.

Evidence and reproduction:

`def f(): return f()` followed by `f()` exits with status 139 in both debug and
release builds. A finite recursion of 1,100 levels succeeds even though
`sys.getrecursionlimit()` reports the default limit of 1,000, confirming that
the public limit is not enforced.

Disproof attempts:

The review traced positional, keyword, method, constructor, native-thunk, and
internal prepared-code entries. None performs a physical stack-bound check.
The frame frontier supports scanning and re-entry but does not enforce stack
capacity.

Recommended fix boundary:

Centralize a checked frame-entry calculation that reserves the complete callee
frame before any header or adapted argument write. Raise `RecursionError` at
the configured limit and retain an independent physical bound guard for large
frames or altered limits. Cover every managed entry path in debug and release.

Verification:

Confirmed in debug and release, with a finite probe demonstrating that the
reported recursion limit is metadata-only. No fix has been implemented.

Disposition:

Open.

### CVR-012: Inherited constructor changes leave stale derived thunks

- Severity: P1
- Status: resolved
- Review unit: R5
- Found at: `ea912bc`
- Affected code: `src/object_model/class_object.cpp:680`,
  `src/object_model/class_object.cpp:717`
- Affected tests: `tests/python/class_constructors.py`

Invariant or semantic rule:

A derived class's construction behavior and accepted call signature must follow
the current `__new__` and `__init__` resolved through its MRO. Mutating an
inherited constructor method must invalidate both the call cache and the
generated constructor thunk that captured the old method.

Reachable path:

After `D()` creates a constructor thunk from `B.__init__` or `B.__new__`, a
write to the method on `B` invalidates `D`'s MRO validity cell. On the next
call, `create_constructor_thunk_slow()` first creates a fresh valid cell, then
returns the existing `D.constructor_thunk` merely because that new cell is
valid. The old thunk is not associated with the invalidated cell. Constructor
thunks are cleared for contents mutations on the class itself, but not when an
attached base invalidates a derived class's MRO cell.

Observable impact:

Derived classes continue calling replaced or deleted inherited constructors,
ignore newly added inherited constructors, and retain stale accepted argument
shapes. Ordinary method lookup observes the mutation correctly; class calls do
not.

Evidence and reproduction:

A base `B.__init__` that sets `self.x = 1` and derived `D(B)` first produce
`D().x == 1`. After assigning a replacement `B.__init__` that sets `x = 2`,
CloverVM still produces `D().x == 1`; CPython produces `2`. Replacing an
inherited `__new__` that returns `1` with one that returns `2` likewise leaves
`D()` returning `1`. Adding `B.__init__(self, x)` after the first `D()` also
leaves `D(x)` incorrectly rejecting the argument.

Disproof attempts:

The review confirmed that base mutation does invalidate the derived MRO cell
and makes the class-call inline cache miss. Ordinary inherited method reads,
replacement by a non-callable, class-chain writes and deletes, multiple
inheritance lookup, and `SlotDict` mutation paths all observed invalidation.
The stale state is isolated to reuse of the generated constructor thunk.

Recommended fix boundary:

Tie each constructor thunk to the validity cell under which its `__new__` and
`__init__` lookup was resolved, or clear the derived thunk when that dependency
is invalidated. Rebuild before pairing a thunk with a newly created cell. Add
interpreter tests for inherited constructor add, replace, and delete across
both `__init__` and `__new__`, including signature changes.

Verification:

Added inherited `__init__` and `__new__` add, replace, and delete coverage,
including signature changes. The constructor regression file passes directly,
and `ninja -C build-debug all check` passes with 1,239 tests in 35 suites and
one disabled test.

Disposition:

Resolved by clearing the cached constructor thunk whenever its MRO
shape-and-contents validity cell is replaced, preventing an old thunk from
being paired with a newly valid cell.

### CVR-015: Cyclic container representation overflows the native stack

- Severity: P1
- Status: open
- Review unit: R7
- Found at: `e2c5c3b`
- Affected code: `src/builtin_types/dict.cpp:150`,
  `src/builtin_types/list.cpp:18`, `src/builtin_types/tuple.cpp:18`,
  `src/builtin_types/string_builder.cpp:20`
- Affected tests: none

Invariant or semantic rule:

Representing recursive builtin containers must terminate using Python's
recursion markers. A supported cyclic object graph must not recursively consume
the native stack.

Reachable path:

Dict, list, and tuple representation recursively append each contained value's
representation through `StringBuilder::append_repr()`. No active-container set
or equivalent recursion guard detects that the same container is already being
formatted.

Observable impact:

`repr()` or `print()` on a directly or indirectly cyclic builtin container
overflows the native stack and terminates the process. CPython renders recursive
positions as `{...}` or `[...]`.

Evidence and reproduction:

Both `d = {}; d['x'] = d; repr(d)` and
`a = []; a.append(a); repr(a)` exit with status 139 in CloverVM. CPython returns
`{'x': {...}}` and `[[...]]`. Indirect tuple/list cycles reach the same path.
ASan identifies native stack overflow through repeated representation calls.

Disproof attempts:

The review checked `StringBuilder`, individual container implementations, and
the general repr dispatch for an existing recursion sentinel or thread-local
active-object guard; none exists. This is distinct from CVR-014 because no
recursive Python function call is involved.

Recommended fix boundary:

Add an exception-safe per-thread representation recursion guard keyed by object
identity, with container-specific recursion markers, and cover direct and
indirect cycles plus cleanup after an element repr raises.

Verification:

Confirmed in debug, release, and ASan+UBSan against CPython. No fix has been
implemented.

Disposition:

Open.

### CVR-016: Sequence search methods compare identity instead of equality

- Severity: P2
- Status: open
- Review unit: R7
- Found at: `e2c5c3b`
- Affected code: `src/builtin_types/list.cpp:516`,
  `src/builtin_types/tuple.cpp:376`
- Affected tests: none

Invariant or semantic rule:

`list.count`, `list.index`, `list.remove`, `tuple.count`, and `tuple.index` use
Python equality for each candidate. Equality calls may return true for distinct
representations, raise exceptions, or mutate observable state.

Reachable path:

The installed search methods compare stored `Value` handles directly. They do
not dispatch `==`, so only identical encodings match and user `__eq__` methods
are never called.

Observable impact:

`[True].count(1)` and `(True,).count(1)` return `0` rather than `1`;
corresponding `index` and `remove` operations raise `ValueError`. Distinct
objects that compare equal are missed, and equality exceptions or side effects
are suppressed.

Evidence and reproduction:

Direct CloverVM probes reproduced incorrect `count`, `index`, and `remove`
results for `True` versus `1`; CPython treats them as equal. Inspection confirms
raw handle comparison in every affected installed method.

Disproof attempts:

Primitive identity is not an accepted fast path without a semantic equality
fallback: bool and SMI already demonstrate different handles with equal Python
values. General membership dispatch uses Python equality and therefore does not
justify the search-method implementation.

Recommended fix boundary:

Route candidate comparison through the existing fallible equality protocol,
propagate exceptions, and define mutation-safe iteration behavior matching the
sequence method contract. Add primitive cross-type and custom-`__eq__` tests.

Verification:

Confirmed with CloverVM and CPython probes. No fix has been implemented.

Disposition:

Open.

### CVR-017: Unicode case conversion loses expanding mappings

- Severity: P2
- Status: open
- Review unit: R7
- Found at: `e2c5c3b`
- Affected code: `src/builtin_types/str.cpp:753`
- Affected tests: none

Invariant or semantic rule:

Python string case conversion follows Unicode mappings, including mappings that
expand one code point into multiple code points. The output length need not
equal the input length.

Reachable path:

`str.upper()` and `str.lower()` transform one `cl_wchar` at a time into one
output element using locale wide-character helpers. That representation cannot
emit expanding Unicode mappings.

Observable impact:

Common non-ASCII strings receive incorrect results: `"ß".upper()` remains
`"ß"` rather than `"SS"`, `"Straße".upper()` produces `"STRAßE"`, and
`"İ".lower()` loses the combining dot required by Python's Unicode mapping.

Evidence and reproduction:

Direct CloverVM and CPython probes reproduced each differing result. The fixed
one-input/one-output loop structurally cannot represent the expected strings.

Disproof attempts:

No later normalization or expansion phase exists. These methods are installed
as supported string operations and the behavior is not recorded as an
intentional deviation.

Recommended fix boundary:

Use a deterministic Unicode case-mapping table or library that supports full
mappings and allocate output for expansion. Pin locale-independent ASCII and
multi-code-point Unicode cases.

Verification:

Confirmed with differential probes. No fix has been implemented.

Disposition:

Open.

### CVR-018: Float floor division mishandles underflowed negative quotients

- Severity: P2
- Status: resolved
- Review unit: R7
- Found at: `e2c5c3b`
- Affected code: `src/builtin_types/float.cpp:214`,
  `src/builtin_types/float.cpp:230`
- Affected tests: none

Invariant or semantic rule:

Float floor division and modulo must preserve Python's coupled quotient and
remainder sign rules even when the mathematical quotient is too small for a
nonzero binary floating-point result.

Reachable path:

Normal and reflected float floor division compute `std::floor(left / right)`
directly. A tiny negative quotient can underflow to negative zero before
`floor`, yielding `-0.0` instead of the required `-1.0`, while the modulo path
still returns a positive divisor-signed remainder.

Observable impact:

Floor division returns the wrong numeric result and becomes inconsistent with
the corresponding modulo result.

Evidence and reproduction:

For `a = -8.122808264515302e-272` and
`b = 7.866340851152702e98`, CPython produces `a // b == -1.0` and
`a % b == b`; CloverVM produces `-0.0` and the same positive remainder.

Disproof attempts:

Integer and BigInt differential fuzzing found no analogous division/modulo
errors. Directed mixed numeric, signed modulo, power, bool-as-int, and shift
probes passed. The reflected float implementation repeats the same direct
division and is affected symmetrically.

Recommended fix boundary:

Implemented a Python-compatible float divmod helper, including remainder sign
adjustment, quotient correction, rounding stabilization, and signed-zero
handling. Normal and reflected floor division and modulo now use the shared
calculation.

Verification:

Added interpreter coverage for the underflowed negative quotient and remainder
through both normal and reflected methods. `ninja -C build-debug all check`
passes with 1,244 tests across 36 suites; one test is disabled.

Disposition:

Resolved locally; uncommitted.

### CVR-019: Exception handler targets remain bound after exit

- Severity: P2
- Status: open
- Review unit: R8
- Found at: `e2c5c3b`
- Affected code: `src/compiler/codegen.cpp:2159`,
  `src/compiler/codegen.cpp:2203`
- Affected tests: none

Invariant or semantic rule:

Python implicitly deletes the name bound by `except ... as name` when the
handler exits, including when that name had a prior binding. Cleanup must occur
on normal and nonlocal exits to preserve visible scope semantics and break
exception/traceback cycles.

Reachable path:

Handler lowering drains and stores the active exception into the target, emits
the body, then transfers directly to common completion. Neither the ordinary
nor saved-original-exception branch emits deletion of the handler target.

Observable impact:

The caught exception remains visible after the handler. A previous binding is
left replaced by the exception rather than becoming unbound, and the retained
reference defeats Python's cycle-breaking cleanup rule.

Evidence and reproduction:

After `try: raise ValueError` and `except ValueError as e: pass`, CloverVM
prints `e`; CPython raises `NameError`. The same difference occurs when `e` was
assigned before the `try`.

Disproof attempts:

The review traced typed and bare handlers, saved exception registers, handler
completion, and cleanup contexts. No later delete runs. Existing finally and
nonlocal-control-flow coverage does not implement target cleanup implicitly.

Recommended fix boundary:

Lower the handler binding as an implicit cleanup region that clears and deletes
the name on normal completion, `return`, `break`, `continue`, and exceptional
exit. Add local, global, prior-binding, nested-handler, and nonlocal-exit tests.

Verification:

Confirmed with CloverVM and CPython. No fix has been implemented.

Disposition:

Open.

### CVR-020: Parenthesized assignment expressions are rejected

- Severity: P2
- Status: open
- Review unit: R8
- Found at: `e2c5c3b`
- Affected code: `src/compiler/parser.cpp:637`,
  `src/compiler/parser.cpp:698`, `src/compiler/parser.cpp:1268`
- Affected tests: none

Invariant or semantic rule:

Where assignment expressions are supported, parenthesized named expressions
are valid expression atoms and may appear on assignment right-hand sides and
as call arguments.

Reachable path:

Parenthesized content is parsed through `genexp()` and ordinary `expression()`.
Only `named_expression()` recognizes `NAME := value`, so the colon-equals token
is left unconsumed before the parser requires the closing parenthesis.

Observable impact:

Valid programs such as `x = (y := 4)` and `print((x := 3), x)` fail with
`SyntaxError: Expected token RPAR, got COLONEQUAL`, despite assignment
expressions working in CloverVM condition contexts.

Evidence and reproduction:

CPython executes both programs and exposes the assigned values. CloverVM
rejects both, while `if x := 3: print(x)` succeeds, demonstrating a grammar hole
inside an implemented syntax feature.

Disproof attempts:

Assignment-expression AST and codegen support exist, and the construct is not
listed as intentionally unsupported. The failure occurs before AST creation,
specifically at the parenthesized grammar route.

Recommended fix boundary:

Use the named-expression grammar at parenthesized expression positions while
preserving generator-expression and tuple disambiguation. Add assignments,
calls, nesting, and invalid-unparenthesized-context parser/interpreter tests.

Verification:

Confirmed with CloverVM and CPython parser/runtime probes. No fix has been
implemented.

Disposition:

Open.

### CVR-021: Star import converts missing exported attributes to ImportError

- Severity: P2
- Status: resolved
- Review unit: R9
- Found at: `e2c5c3b`
- Affected code: `src/import_system/import_system.cpp:731`
- Affected tests: none

Invariant or semantic rule:

After `from module import *` obtains `module.__all__`, each listed name is an
attribute retrieval. A missing listed attribute raises `AttributeError`, unlike
an explicit `from module import name` failure.

Reachable path:

Star import loops over `__all__` but reuses `import_from()`, whose explicit
from-import contract converts a missing attribute into `ImportError`.

Observable impact:

Modules with a stale or dynamic `__all__` expose the wrong exception type and
message to importers, preventing callers from distinguishing a broken export
list from an unavailable explicit import.

Evidence and reproduction:

Setting an existing module's `__all__` to `("nope",)` and executing
`from module import *` raises `ImportError` in CloverVM and `AttributeError` in
CPython.

Disproof attempts:

This does not depend on package child-import fallback and is distinct from
CVR-002. The module is already loaded and the missing name is purely an
attribute named by `__all__`.

Recommended fix boundary:

Star and explicit from-import now share package-child discovery while retaining
caller-specific missing-name errors. Star import raises `AttributeError` for a
proven missing `__all__` export, but still loads existing package children and
propagates child-load failures.

Verification:

Added coverage for a missing `__all__` export and retained coverage for loading
an existing package child through `__all__`. The full debug gate passes with
1,248 tests across 36 suites; one test is disabled.

Disposition:

Resolved locally; uncommitted.

### CVR-022: Relative import ignores the module spec parent fallback

- Severity: P2
- Status: resolved
- Review unit: R9
- Found at: `e2c5c3b`
- Affected code: `src/runtime/virtual_machine.cpp:325`
- Affected tests: none

Invariant or semantic rule:

Explicit relative import derives package context from `globals['__package__']`
when valid and falls back to `globals['__spec__'].parent` when the package value
is `None`. The spec is the modern source of import metadata.

Reachable path:

The builtin import implementation reads only `__package__` and immediately
raises `ImportError` when it is `None`, without consulting a valid module spec.

Observable impact:

Relative imports fail in module globals whose package field was cleared or
otherwise left `None` even though their spec identifies the correct parent.

Evidence and reproduction:

Calling `__import__` at level one with globals containing
`__package__ = None` and a real imported module's `__spec__` succeeds and
returns the sibling in CPython. CloverVM raises
`ImportError: attempted relative import with no known parent package`.

Disproof attempts:

The reviewed `ModuleSpecObject` stores parent metadata, and the import design
explicitly names spec parent as the modern source of truth. The failure occurs
before module search, so finder or `sys.modules` behavior cannot repair it.

Recommended fix boundary:

Relative-name resolution now gives an explicit string `__package__`
precedence, falls back to a valid `ModuleSpecObject.parent` when `__package__`
is `None` or absent, and rejects other package values with `TypeError`. The
deprecated `__name__`/`__path__` fallback remains unsupported.

Verification:

Added direct builtin-hook coverage for `None`, missing, empty, and invalid
`__package__` values, plus an ordinary module that clears `__package__` before
a relative import. `ninja -C build-debug all check` passes with 1,253 tests
across 36 suites; one test is disabled.

Disposition:

Resolved locally; uncommitted.

### CVR-023: Public value APIs mishandle propagated error handles

- Severity: P2
- Status: resolved
- Review unit: R9
- Found at: `e2c5c3b`
- Affected code: `src/api/extension_api.cpp:250`,
  `src/api/extension_api.cpp:271`, `src/api/extension_api.cpp:301`,
  `src/api/extension_api.cpp:338`, `src/api/extension_api.cpp:365`,
  `src/api/extension_api.cpp:386`
- Affected tests: `tests/native_modules/_test_native.c`,
  `tests/test_native_module_build.cpp`

Invariant or semantic rule:

The public error-marker handle is a sanctioned propagation value. Passing it
through another C API value consumer must return failure without replacing the
pending exception or treating two markers as ordinary identical values.

Reachable path:

Tuple size/item, string conversion, float conversion, and integer conversion
directly unwrap and type-check handles, so an error marker overwrites the
original exception with a conversion `TypeError`. `clover_is` directly compares
the marker values and returns success, including when both arguments are the
same propagated error marker.

Observable impact:

Native extension code that chains a failed helper result into another public
API loses the original exception or receives a false successful identity
answer while pending failure remains.

Evidence and reproduction:

Control-flow tracing confirms every listed API bypasses the sentinel-aware
`unwrap_extension_value()` used by the newer dictionary APIs. The public raise
helpers and `clover_propagate_error()` produce exactly the reachable marker
handle in question; tuple construction already detects it correctly.

Disproof attempts:

Fabricated opaque handles remain outside the API contract, but the error marker
is explicitly public. Ordinary wrong-type and bounds failures behave as
documented. CVR-005 and CVR-006 concern callback/module success with pending
state, not corruption while consuming the propagation handle.

Recommended fix boundary:

Route all public value consumers through one sentinel-aware unwrap helper that
preserves existing pending state, initializes outputs consistently on failure,
and never exposes marker identity as a successful result. Add native harness
tests for every consumer.

Verification:

Added a native callback that passes a genuine propagated error handle through
all six affected consumers, checks deterministic failure outputs, and confirms
that the original `OverflowError` remains pending. The focused native tests
pass, and `ninja -C build-debug all check` passes with 1,241 tests in 35 suites
and one disabled test.

Disposition:

Resolved by routing all public value consumers through the sentinel-aware
unwrap helper, initializing outputs before validation, and preventing
`clover_is` from comparing error markers as ordinary values.

### CVR-001: Slab allocation advances beyond its mapped extent

- Severity: P2
- Status: resolved
- Review unit: R1
- Found at: `c289490`
- Affected code: `src/memory/slab_allocator.h`,
  `src/memory/slab_allocator.cpp`, `src/memory/global_heap.cpp`
- Affected tests: `tests/test_heap.cpp`

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

Added direct coverage for exact aligned exhaustion, rejection of an incomplete
tail slot, and `SIZE_MAX`, plus a non-aligned dedicated-allocation test. The
focused allocator tests pass, and `ninja -C build-debug all check` passes with
1,243 tests in 36 suites and one disabled test. Release assembly retains one
failure branch before the existing rounding and cursor update. Targeted release
benchmarks for class instantiation and memory reclamation showed no regression
against the pre-change baseline.

Disposition:

Resolved by precomputing the complete-slot allocation boundary for each slab,
checking the request against remaining allocatable bytes before rounding, and
sizing dedicated slabs for the rounded payload extent.

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
- Status: accepted
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

No runtime fix. Extension callbacks are trusted native code, and checking this
contract after every callback would add work to all valid extension calls only
to diagnose an extension bug. Keep the normal callback path unchanged and make
the trust boundary explicit in `doc/clover-c-api.md`.

Verification:

Established from the extension contract and complete opcode paths. The C API
documentation now explicitly states that callbacks must preserve the
result/pending-exception pairing and that the VM does not validate a normal
return with a pending exception.

Disposition:

Accepted as a trusted-extension contract. No runtime change.

### CVR-006: Native module initialization accepts success with an exception

- Severity: P2
- Status: resolved
- Review unit: R2
- Found at: `e7bd814`
- Affected code: `src/native/native_module_loader.cpp:162`
- Affected tests: `tests/test_native_module_build.cpp`,
  `tests/native_modules/_test_native_success_with_exception.c`

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

Added a malformed native module that sets an exception and returns
`CLOVER_STATUS_OK`. The focused native-module status-matrix tests pass, and
`ninja -C build-debug all check` passes with 1,240 tests in 35 suites and one
disabled test.

Disposition:

Resolved by rejecting successful native initialization when an exception is
pending, replacing the inconsistent state with `SystemError`, and allowing the
normal import failure cleanup to remove the module from `sys.modules`.

### CVR-007: Dictionary iterators revive after invalidation

- Severity: P2
- Status: resolved
- Review unit: R3
- Found at: `b9ed3d9`
- Affected code: `src/builtin_types/dict_view.cpp:99`,
  `src/builtin_types/dict_view.h:79`
- Affected tests: `tests/test_dict.cpp`

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

Added shared regression coverage for key, value, and item iterators that
restores the original dictionary size after the first mutation error. The
focused test passes, and `ninja -C build-debug all check` passes with 1,244
tests in 36 suites and one disabled test.

Disposition:

Resolved by poisoning the existing expected-size field with `-1` before
raising the first mutation error, ensuring every later size comparison fails
without changing iterator layout or the successful `__next__` path.

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

### R5: Object Model, Attributes, Shapes, And Caches At `ea912bc`

- Scope: instance and class attribute lookup, descriptor classification and
  precedence, attribute writes and deletes, shape transitions and overflow
  storage, MRO and contents validity cells, attribute inline-cache replay,
  `__class__` reassignment, class calls, generated constructor thunks, and
  adjacent class/metaclass behavior.
- Design documents read: `descriptor-execution.md`, `object-model.md`,
  `builtin-object-model.md`, `python-deviations.md`, and the object-model
  priorities in `development-priorities.md`.
- Code and tests reviewed: `object_model/attr.cpp`, attribute descriptors and
  caches, shapes and validity cells, `object.cpp`, `class_object.cpp`,
  constructor-thunk generation, attribute and class-call interpreter handlers,
  and focused attribute, shape, method, and interpreter tests.
- Confirmed findings: CVR-012.
- Investigations: none.
- Verification commands: focused attribute/method tests; direct CloverVM and
  CPython inherited-constructor differential probes; cached class read,
  deletion, `__class__` reassignment, and allocation-gap probes; ASan+UBSan
  stress across 100,000 class mutations and allocations; and
  `ninja -C build-debug all check`.
- Verification result: direct probes reproduced CVR-012 for inherited
  `__init__`, inherited `__new__`, and post-cache signature changes. Focused
  lookup tests passed, mutation/cache stress passed under ASan+UBSan, and the
  final debug gate passed all enabled tests.
- Unreviewed edges: descriptor `__get__`, `__set__`, and `__delete__`
  execution, custom `__getattribute__`/`__getattr__`, custom-metaclass Python
  construction, complete `type` behavior, and some `__bases__` mutation paths
  are documented incomplete features rather than implemented R5 surfaces.
- Residual risk: class-object lookup currently searches the class MRO before
  the metaclass MRO, so a metaclass data descriptor cannot win as Python
  requires; this is latent because custom metaclasses are not Python-
  constructible yet. Attribute-cache payloads are non-owning pointers; stress
  found no lifetime failure, but an every-safepoint reclamation test for a
  dormant invalidated cache would strengthen the proof. Instance `__dict__`
  and class `__dict__` intentionally use live `SlotDict` views rather than
  CPython's exact exposure model.

### R6: Calls, Frames, And Argument Adaptation At `477e1af`

- Scope: positional and keyword calls, fixed/default/full adaptation, grouped
  signature binding, positional-only and keyword-only parameters, callee
  `*args` and `**kwargs`, caller/callee frame windows, call caches,
  continuations, direct methods, class calls, constructor thunks, native
  thunks, and managed/native entry bridges.
- Design documents read: `function-calling-convention.md`,
  `function-call-adaptation.md`, `native-managed-boundaries.md`,
  `inline-cache-slot-layout.md`, `python-deviations.md`, and the call priorities
  in `development-priorities.md`.
- Code and tests reviewed: call and method lowering in `codegen.cpp`, call
  emitters and frame sizing, positional/keyword/method/special-method handlers,
  `Function` signature metadata, keyword and function call caches, constructor
  thunks, native functions and extension entry, frame-frontier management, and
  focused parser, codegen, interpreter, constructor, and native tests.
- Confirmed findings: CVR-013, CVR-014. R6 also expanded CVR-011 with the
  explicit-keyword call crash.
- Investigations: none.
- Verification commands: focused debug and release call/adaptation tests;
  direct CloverVM and CPython method-ordering probes; generated large-keyword
  calls; finite and unbounded recursion probes in debug and release; release
  opcode-frame checking; and `ninja -C build-debug all check`.
- Verification result: method replacement, missing-method, and keyword probes
  reproduced CVR-013; debug and release recursion reproduced CVR-014; a
  124-keyword call reproduced the additional CVR-011 crash. Focused call,
  binding, constructor, method, codegen, and native tests passed, the release
  frame checker passed, and the final debug gate passed all enabled tests.
- Unreviewed edges: caller `*args` and `**kwargs` expansion, arbitrary callable
  objects, descriptor-produced callables, and full combined custom
  `__new__`/`__init__` construction are documented incomplete features rather
  than implemented R6 surfaces.
- Residual risk: call-cache payloads are non-owning pointers that rely on
  guard/owner lifetime conventions; no reclamation failure was reproduced.
  Exact CPython diagnostic wording is intentionally not complete. Existing
  byte-sized argument counts remain part of CVR-011's width boundary rather
  than a separate finding.

### R7: Builtin Types And Operators At `e2c5c3b`

- Scope: numeric builtins and SMI/BigInt transitions, normal/reflected operator
  dispatch, `NotImplemented`, strings and Unicode, lists, tuples, slices,
  ranges, iterators, container repr, sequence searches, hashing/equality
  integration, membership, subscription, and trusted-handler cache replay.
- Design documents read: `fast-operator-dispatch.md`, `bigint.md`,
  `iteration-plans.md`, `python-deviations.md`, and builtin/operator priorities
  in `development-priorities.md`.
- Code and tests reviewed: builtin type implementations, operator tables and
  walk/cache machinery, string building and repr dispatch, sequence/slice
  normalization, range loops, numeric kernels, trusted resolver registrations,
  and focused builtin, BigInt, operator, sequence, and interpreter tests.
- Confirmed findings: CVR-015, CVR-016, CVR-017, CVR-018.
- Investigations: none.
- Verification commands: focused debug and release builtin/operator tests;
  integer/BigInt differential fuzzing; directed reflected, mixed-numeric,
  Unicode, sequence-search, and cyclic-repr probes; ASan+UBSan cyclic-repr
  reproduction; release opcode-frame checking; and
  `ninja -C build-debug all check`.
- Verification result: direct probes reproduced all four findings. Numeric
  fuzzing and directed operator probes found no additional differences, focused
  debug/release suites passed, cyclic repr reproduced under sanitizers, and the
  final debug gate passed all enabled tests.
- Unreviewed edges: absent list/tuple structural equality and sequence
  repetition, float and tuple hashing, set-like dict-view operations, and other
  wholly absent builtin methods are missing feature coverage rather than
  implemented-path defects under the ledger rules.
- Residual risk: non-SMI range and slice behavior, including bool range
  arguments and oversized slice-field clipping, is explicitly documented as
  incomplete. Exact Unicode behavior beyond the directed casing probes was not
  exhaustively compared against the Unicode database. Mutable callbacks during
  every sequence method remain broader than the exercised search paths.

### R8: Compiler And Python Semantics At `e2c5c3b`

- Scope: tokenizer/parser grammar, AST and binding analysis, assignments and
  deletion, globals/locals and class scope, expression evaluation order,
  comparisons and short-circuiting, loops, calls, imports at the compiler
  boundary, and `try`/`except`/`else`/`finally`/`with` cleanup lowering.
- Design documents read: `python-opcode-design-notes.md`,
  `python-deviations.md`, `development-priorities.md`, and compiler/runtime
  sections of `architecture.md`.
- Code and tests reviewed: tokenizer and parser expression/statement grammar,
  AST analysis, codegen lowering and cleanup contexts, exception tables,
  assignment/subscript/attribute operations, comparison and membership
  handlers, class body execution, import lowering, and focused parser, codegen,
  and interpreter tests.
- Confirmed findings: CVR-019, CVR-020.
- Investigations: none.
- Verification commands: focused parser/scope/assignment and
  exception/control-flow suites; side-effecting CloverVM/CPython differential
  programs; direct exception-target and assignment-expression probes; and
  `ninja -C build-debug all check`.
- Verification result: direct probes reproduced both findings. Comparison,
  boolean, membership, assignment, loop, finally, with, and import-order probes
  found no additional supported-path differences. Focused suites and the final
  debug gate passed.
- Unreviewed edges: chained and unpacking assignment targets, comprehensions,
  lambdas, closures/nonlocal capture, Python-correct dynamic class scope,
  custom truthiness, descriptor execution, and metaclass keywords are explicit
  missing or documented incomplete features.
- Residual risk: no broad randomized AST differential campaign was run. The
  cleanup-state matrix is large despite strong focused coverage, and future
  closure support must ensure handler-target deletion clears captured cells as
  well as ordinary locals.

### R9: Imports, Native Modules, And Public APIs At `e2c5c3b`

- Scope: source/package/native import state, `sys.modules`, partial
  initialization and retry, cycles, fromlist/star and relative imports, module
  metadata, filesystem/native loader failures, builder initialization, public
  extension handles, argument validation, ownership, and native-managed entry.
- Design documents read: `import-system-design.md`,
  `module-global-namespace-design.md`, `native-c-modules.md`,
  `native-managed-boundaries.md`, and `clover-c-api.md`.
- Code and tests reviewed: import finder/loader and builtin import paths,
  `ModuleSpecObject`, source/native module execution, dynamic library caching,
  native module builder APIs, all declarations and implementations in the
  extension C API, call frontier publication, and focused import/native/API
  tests.
- Confirmed findings: CVR-021, CVR-022, CVR-023.
- Investigations: none.
- Verification commands: focused import tests; debug and release native/API
  tests; direct CloverVM/CPython star and relative import probes; public handle
  contract tracing; and `ninja -C build-debug all check`.
- Verification result: differential probes reproduced CVR-021 and CVR-022;
  contract/implementation tracing confirmed CVR-023 against the marker-aware
  APIs. Focused import and native suites passed in debug and release, and the
  final debug gate passed all enabled tests.
- Unreviewed edges: full importlib meta-path/path-hook protocols, namespace and
  zip packages beyond the implemented finder, reload, Windows dynamic loading,
  stable third-party ABI compatibility, and general CPython C API compatibility
  are explicit non-goals or future surfaces.
- Residual risk: the native library init mutex is unused and no broader import
  lock exists, but current execution did not expose a provable concurrent init
  path. Native libraries intentionally remain loaded. ABI version negotiation
  is intentionally absent for VM-version-matched modules, and current POSIX
  wrappers' errno-to-`ValueError` policy was not reclassified here.

### R10: Cross-Cutting Adversarial And Release Review At `e2c5c3b`

- Scope: callback mutation across caches and containers, allocation during call
  and operator adaptation, exceptions during cleanup and failed imports,
  native re-entry, large integers entering narrow consumers, dormant cache
  lifetime, reclamation, allocation arithmetic, builder/interpreter bounds,
  `NDEBUG` divergence, and release hot-handler frame shape.
- Design documents read: the campaign's prior R1-R9 design set plus
  `function-calling-convention.md`, `fast-operator-dispatch.md`,
  `native-managed-boundaries.md`, and release checker configuration.
- Code and tests reviewed: cross-subsystem cache guards and payload lifetimes,
  call/operator continuations, cleanup lowering, import teardown, native frame
  frontier, heap reclamation, dynamic layout sizing, VM arrays, BigInt scratch
  capacity, bytecode/table/index builders, interpreter operands, and hot opcode
  symbols.
- Confirmed findings: none new. Adversarial repr probes reconfirmed CVR-015.
- Investigations: none.
- Verification commands: focused debug/release adversarial and boundary tests;
  ASan+UBSan cache, reclamation, failed-import, and native-reentry suites;
  10,000-iteration dormant/inherited-method cache stress; strict release opcode
  frame checking; direct cyclic-container probes; and
  `ninja -C build-debug all check`.
- Verification result: focused sanitizer testing passed 85 tests with the known
  macOS libc++ container-overflow detector disabled; cache/allocation stress
  completed without a sanitizer report; 89 focused release invariant tests and
  the opcode-frame checker passed; cleanup/mutation probes produced no new
  defect; and the final debug gate passed all enabled tests.
- Unreviewed edges: future multithreaded execution, JIT entry/deoptimization,
  moving-GC barriers, full cycle collection, Windows loading, and unsupported
  Python protocols are not current cross-cutting implementation surfaces.
- Residual risk: non-owning cache payload pointers still rely on validity and
  owner lifetime conventions; a purpose-built every-safepoint reclamation test
  would strengthen the dormant-cache proof. Very large shape/parameter metadata
  reaches earlier limits before its assertion-backed terminal bounds. The
  packed-read portability decision remains rejected as CVR-010, and the
  warning-only frame-target policy remains rejected as CVR-008; neither was
  reopened without new target evidence.

## Resolved And Rejected Index

Keep a compact index here after entries acquire final dispositions:

| ID | Type | Final status | Resolution |
| --- | --- | --- | --- |
| CVR-008 | finding | rejected | Warning-only frame reporting is intentional policy. |
| CVR-010 | finding | rejected | Packed reads are an isolated target-specific primitive. |
