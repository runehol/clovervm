# Regular Expression Engine

| Field | Value |
|---|---|
| Document type | Design |
| Status | Draft |
| Implementation | Not started |
| Scope | C++ regular-expression parser, bytecode compiler, backtracking matcher, and Python `re` integration |
| Owning layers | Regex parser/compiler own pattern syntax and lowering; regex executor owns matching; runtime object helpers and the stdlib binding own Python objects, lifetime, and exceptions |
| Validated against | N/A; future design, with semantic probes against local CPython 3.14.5 |
| Supersedes | N/A |

This draft records the agreed engine design. Implementation has not started;
remaining API and encoding decisions are listed at the end. Agree on a concrete
implementation slice before changing code.

Execution follows CPython 3.14.5 with suspended matching contexts and selective
capture saves. The [execution model](#execution-model) summarizes the agreed
contracts; implementation has not begun.

The document follows the compilation and execution pipeline:

- [Architecture](#module-boundary) and [pattern storage](#pattern-categories-and-subject-storage).
- [Parser and compiler](#parser-and-compiler-representation), [Unicode data](#shared-unicode-data), and [program ownership](#compiled-program-and-builder).
- [Bytecode and execution](#bytecode-and-execution), including backtracking and class storage.
- [Search](#search-metadata), [result objects](#pattern-and-match-objects), [iteration](#iteration-and-empty-match-progression), and [replacement](#replacement-programs-and-substitution).
- [Errors](#error-behavior), [slice 1](#slice-1-program-construction-and-inspection), and [open decisions and verification](#open-decisions-and-verification).
- [Worked execution traces](#execution-trace-examples) and [review follow-up](#adversarial-review-follow-up).

## Agreed Direction

Implement the pattern parser, compiler, and matching engine in C++. Start from
CPython's `re` / `_sre` approach: compile patterns into structured regex
bytecode and execute that bytecode with a backtracking interpreter. Use one
implementation with templates for the bytes and Unicode variants, rather than
maintain separate engines.

"Compiled" means runtime compilation of user-supplied patterns into executable
regex bytecode. Native machine-code generation is not required. CTRE remains
useful for C++-known tokenizer patterns, but cannot supply this runtime-pattern
compiler.

The immediate priority is a correct, fast basic engine. Pattern caching is
explicitly deferred, including cache ownership, keys, eviction, and
synchronization. If caching is added later, it must be multithread safe. No
cache is needed to reuse an explicitly retained compiled pattern.

Automata execution, regex JIT, and aggressive new optimizations are outside the
initial direction. Backtracking remains potentially expensive on adversarial
patterns; removing native recursion does not remove exponential search.

### Execution Model

Use CPython-style suspended matching contexts and handler-selected capture
preservation. `MARK` writes live slots directly. Handlers save validity and
last-group metadata and copy active mark prefixes where the reference requires
it. Child calls use explicit contexts and internal resume states, avoiding
native recursion for those continuations. See the pinned
[capture and context machinery](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L327).

The agreed contracts and implementation verification areas are:

| ID | Contract | Verification |
|---|---|---|
| ES-01 | Context fields, call/return, scratch ownership | Success/failure/error exits and release order |
| ES-02 | Live marks, validity metadata, selective prefix saves | Nested capture restoration and reuse |
| ES-03 | `BRANCH` chains and shared-tail child attempts | Alternative priority and tail rollback |
| ES-04 | General-repeat records, endings, and continuations | Nested, empty, greedy/lazy, and assertion cases |
| ES-05 | Local assertion/atomic completion with `SUCCESS` | Polarity, retained captures, and commitment |
| ES-06 | Simple tail retries and dedicated possessive execution | Retry bounds, per-iteration commitment, and failed attempts |
| ES-07 | Scoped helpers, explicit root completion, numeric opcodes, three-pass verifier | Slice 1 program-construction and inspection tests |
| ES-08 | Differential and resource validation | Reference outcomes, forced growth/failure, and memory/context costs |

Use inline-capacity scratch vectors with stable indices; choose capacities from
measurements. Shared program storage is immutable. Runtime polling/safepoint
integration remains deferred under AR-12, independently of these contracts.

## Module Boundary

Use a thin public Python `re` module over a private native `_re` module,
following the public-module/private-helper pattern already used in the stdlib.

| Layer | Responsibility |
|---|---|
| Shared Unicode layer | Classification, case mappings, equivalence groups, name lookup |
| C++ regex engine | Parser, AST, analysis, compiler, immutable program, matcher |
| Native `_re` binding | `Pattern`, `Match`, match iterator, Python argument/result adaptation and exceptions |
| Python `re` module | Public names, flag-facing API, module-level convenience functions, small utilities |

Module-level functions delegate to compiled patterns, for example:

```python
def search(pattern, string, flags=0):
    return _compile(pattern, flags).search(string)
```

The Python `_compile` helper handles already compiled `Pattern` arguments and
the associated flag validation, or invokes the native compiler. Pattern caching
remains explicitly deferred. The private `_re` interface serves CloverVM's
implementation; it need not reproduce CPython's private `_sre` bytecode API.

Keep `Pattern.search`, `match`, `fullmatch`, `finditer`, `findall`, `split`,
`sub`, and `subn` native. Repeated search, capture extraction, and output
construction stay together. A native match iterator owns the shared progression
rules, including empty matches, rather than each consumer implementing those
rules independently; see [iteration](#iteration-and-empty-match-progression).
Result shapes follow [result construction](#result-construction).

Replacement-template compilation and interpretation are C++, following the
separate [replacement program](#replacement-programs-and-substitution).
Callable replacements use the ordinary runtime Python call API and the
CPython-compatible fragment-retention rules in the replacement section.

The native binding provides `PatternError`, `Pattern`, and `Match`, exposed
through the public `re` API; Python re-exports these and aliases `error =
PatternError`. The engine returns typed diagnostics and the binding constructs
Python exceptions. Python owns the flag-facing API and small utilities such as
`escape`.

The engine owns regex semantics over subject elements, the binding owns
CloverVM object adaptation, and the Python module supplies the convenience API.
This boundary requires neither access to interpreter frames nor a new public C
API.

The regex compiler emits regex bytecode, independently of the Python language
parser and VM bytecode compiler. The matcher consumes subject views and returns
capture boundaries or an explicit execution error. Compilation and each match
own their mutable scratch state; sharing a program does not share captures or
backtracking stacks or establish thread safety for mutable subjects.

Use the existing [native boundary](native-managed-boundaries.md) and
[typed-handle/fallibility](typed-handles-and-expected.md) contracts: managed
heap references use `Member<...>`, and native local owners use `Owned<...>`
where needed. Execution-time storage access follows existing runtime/buffer
contracts; the matcher's [non-safepoint boundary](#interrupt-polling-and-safepoint-integration)
is explicit. Native-resource cleanup and concrete binding APIs must implement the ownership model described here.

## Pattern Categories And Subject Storage

Bytes and Unicode are different matching categories, independent of element
storage width.

| Pattern category | Literal pool elements | Subject elements | Group result |
|---|---|---|---|
| Bytes | `uint8_t` | 8-bit bytes | `bytes` |
| Unicode | `uint32_t` code points | 8-, 16-, or 32-bit compact Unicode | `str` |

Unicode storage is compact code-point storage: each element represents one
Python character, with omitted upper bits understood to be zero. An 8-bit
Unicode string is not UTF-8, and a 16-bit Unicode string is not UTF-16. Lone
surrogate code points remain representable. Capture offsets count code points
for Unicode and bytes for byte subjects.

The intended execution combinations are:

```text
bytes program   x uint8_t byte subject
Unicode program x uint8_t Unicode subject
Unicode program x uint16_t Unicode subject
Unicode program x uint32_t Unicode subject
```

The same compiled Unicode program must work with all three Unicode subject
widths. Subject comparisons widen elements without truncating pattern literals.
A Unicode literal outside the subject's representable range cannot match that
subject element. Unicode predicates and case rules apply even to an 8-bit
Unicode subject; storage width never selects bytes semantics.

Share parsing, compilation, instruction definitions, backtracking, repetition,
and capture machinery where practical. Specialize literal storage, subject
access, and character operations. `CompiledProgram<Literal>` and
`ProgramBuilder<Literal>` use the literal types above; subject-access template
parameters are a later matcher detail. Separate literal widths do not require
separate opcode definitions or an 8-bit instruction format for bytes programs.

The current runtime uses `wchar_t`-based `String` storage and an 8-bit `Bytes`
type. This design prepares the matcher for future compact Unicode widths; it
does not authorize or require changing the VM string representation now.

## Python Semantics

CPython 3.14 is the semantic reference; the local probes used 3.14.5. The goal
is Python `re` behavior, rather than general Perl/PCRE or POSIX compatibility.
The first implemented API and syntax subset has not yet been selected.

String patterns reject bytes-like subjects, and bytes patterns reject Unicode
subjects. Unicode matches return `str` groups; byte matches return `bytes`
groups. A `Match` retains the original subject and compiled pattern, with
capture boundaries describing its groups.

CPython also accepts contiguous bytes-compatible buffer subjects, including
`bytearray`, contiguous `memoryview`, and compatible `array.array` buffers. It
interprets them as raw bytes, regardless of buffer element format, and rejects
noncontiguous strided views. Pattern inputs do not generally accept arbitrary
buffer objects. CloverVM buffer support and the initial supported subject set
must be assessed separately; this draft does not claim those protocols exist.

Mutable subjects make ownership observable: a local CPython probe matched a
`bytearray`, mutated it, and obtained the changed contents from a later
`group()` call. Eagerly copying all captures would change that behavior.

Compatibility includes branch priority, greediness/laziness, capture
restoration, backreferences, lookarounds and fixed-width lookbehind, group
conditionals, atomic groups, possessive repetitions, and scoped flags. Unicode
categories and case rules must follow Python, including `ASCII` and bytes
`LOCALE` behavior. Zero-width iteration, replacement strings and callbacks,
pattern/match metadata, argument handling, and pattern errors also need
explicit coverage as their APIs are introduced. Supporting the same syntax
alone does not establish compatibility.

## Parser And Compiler Representation

Use a full typed regex AST, separate from the Python language AST. The parser
owns syntax, source spans, effective scoped flags, and group definitions and
references. Analysis derives widths and search metadata; the compiler expands
classes, forms literal runs, and selects instructions through the program
builder. Freeze finalizes and publishes the program, then compilation discards
the tree. Do not emit matching bytecode directly from the parser or introduce a
second lowered tree initially.

Parse literals individually so quantifiers bind to the correct atom. Preserve
unexpanded class elements until compilation. Group-reference validation and
lookbehind checks preserve Python's diagnostic numbering and error precedence.

### AST Nodes And Storage

`ParsedPattern` owns a vector of nodes, its root index, and group/name tables.
Nodes refer to one another through `uint32_t` indices, not owning pointers. Use
a typed `std::variant` payload and inline child lists, following the existing
Python AST's use of indexed storage and small inline vectors while keeping
regex-specific types separate:

```cpp
using NodeId = uint32_t;
using NodeList = absl::InlinedVector<NodeId, 4>;

struct SourceSpan
{
    uint32_t begin;
    uint32_t end; // Exclusive.
};

struct AstNode
{
    SourceSpan source;
    uint32_t effective_flags;
    NodePayload payload; // std::variant of the payload types below.
};
```

Spans count original pattern code points for Unicode or bytes for bytes
patterns, including escapes and ignored verbose-mode text. With these fields,
pattern source length is limited to `UINT32_MAX` elements; report overflow
rather than truncate. Capture indices are zero-based internally and translated
to Python's numbering for diagnostics and public results. `NoCapture` is the
reserved `UINT32_MAX` capture-index value.

| Payload | Fields |
|---|---|
| `SequenceNode` | `NodeList children` |
| `AlternativeNode` | `NodeList branches`, preserving source order |
| `LiteralNode` | `uint32_t code_point` |
| `ClassNode` | Outer negation and inline vector of class elements |
| `AnyNode` | No payload; `DOTALL` comes from effective flags |
| `AnchorNode` | Anchor kind |
| `GroupNode` | Body node and capture index, or `NoCapture` |
| `RepeatNode` | Body, minimum, maximum, greedy/lazy/possessive mode |
| `AssertionNode` | Body, ahead/behind, positive/negative |
| `AtomicNode` | Body node |
| `BackreferenceNode` | Capture index |
| `ConditionalNode` | Capture index, yes branch, no branch |

An empty sequence represents an empty match, including an omitted conditional
else branch. Alternative branches are sequence nodes. Keep noncapturing groups
as `GroupNode` to retain source spans and scoped structure. A repeat stores its
bounds as `uint32_t` with the agreed unbounded-maximum sentinel; simple versus
general repeat is a compiler choice, not an AST distinction. Anchor kinds
distinguish source semantics such as `^` versus `\A`; lowering uses effective
flags to choose the final `AT` test. Lookbehind distance is derived by
analysis, not parsed as a distance.

Class elements preserve source structure before normalization:

```cpp
struct ClassLiteral { uint32_t code_point; };
struct ClassRange { uint32_t first, last; }; // Inclusive.
struct ClassCategory { Category predicate; };

struct ClassElement
{
    SourceSpan source;
    std::variant<ClassLiteral, ClassRange, ClassCategory> payload;
};
```

`ClassNode` owns an `absl::InlinedVector<ClassElement, 4>`. Compilation merges
ranges, expands fixed-rule ignorecase membership, and selects the class
representation. Analysis results live alongside nodes rather than in syntax
payloads. Discarding the AST does not recursively destroy owning child chains.

### Scoped Flags

Scoped flags change behavior within a noncapturing group and restore the
enclosing behavior outside it. For example, `(?i:abc)DEF` matches `AbCDEF` but
not `AbCdef`; `(?i:a(?-i:b)c)` makes only the middle `b` case-sensitive. These
settings primarily affect code generation, with `VERBOSE` also affecting how
the parser reads the body.

| Inline flag | Effect |
|---|---|
| `i` | Ignore case |
| `m` | Line-aware `^` and `$` |
| `s` | Dot includes newline |
| `x` | Verbose parsing |
| `a` | ASCII character mode |
| `u` | Unicode character mode, Unicode patterns only |
| `L` | Locale character mode, bytes patterns only |

Pass effective flags by value into group parsing. Parse and validate each
scoped header's added/removed flags, derive the body's flags, parse the body
under those flags, then continue with unchanged enclosing flags. `i`, `m`, `s`,
and `x` may be enabled or disabled. `a`, `u`, and `L` replace the enclosing
character mode, are mutually exclusive within a setting, and cannot follow `-`.
After validation, derive flags as follows:

```cpp
uint32_t flags = enclosing_flags;
if (added & CharacterModeMask)
    flags &= ~CharacterModeMask;
flags = (flags | added) & ~removed;
```

Store effective flags on AST nodes. The compiler selects instructions and
expands fixed-rule classes accordingly; the matcher needs no mutable flag state
or flag-restoration instructions. Locale mode selects dynamic locale operations
without binding the actual locale during compilation.

Verbose mode takes effect immediately while reading a scoped body. Ignore
ordinary pattern whitespace and `#` comments outside character classes, while
preserving escaped whitespace/`#` and their literal meaning inside classes. Do
not apply whitespace skipping indiscriminately inside regex syntax tokens.

Handle whole-pattern declarations such as `(?im)` separately, with the
reference parser's expression-start restrictions and diagnostics. Retain the
resulting global flags for `Pattern.flags`; scoped settings do not change that
public value. Malformed inline settings raise positioned `PatternError`, while
invalid flag arguments follow [Error Behavior](#error-behavior).

### Group Definitions And References

The parser owns a capture-definition table and a name-to-index map. Allocate a
capture index at its opening parenthesis, before reading its body. Number
captures in opening-parenthesis order, including named captures. Internal
indices are zero-based; Python numbers are one-based. The whole match (Python
group zero) remains separate. Noncapturing groups, assertions, and atomic
groups allocate no capture index.

```cpp
struct GroupInfo
{
    SourceSpan definition;
    NodeId body;         // NoNode while open.
    uint32_t name_index; // NoName for unnamed captures.
};
```

Register the name, if any, when opening the capture. Set its body node when
closing it. Derived capture widths belong in the analysis cache. Names must be
unique Python identifiers; bytes-pattern names must also be ASCII. Compare
names exactly, without Unicode normalization or case folding. Local CPython
3.14.5 probes accept `K` and `K` as distinct group names. Unicode identifier
validation uses the shared predicates under Unicode Identifier Validation,
distinct from regex word membership.

| Reference | Validation |
|---|---|
| Numeric backreference, `\1` | Capture must already exist and be closed |
| Named backreference, `(?P=name)` | Name must already exist and capture must be closed |
| Named conditional, `(?(name)yes\|no)` | Name must already exist; capture may be open |
| Numeric conditional, `(?(1)yes\|no)` | Forward reference allowed; check eventual existence after parsing |

Keep pending forward numeric conditionals with their requested number and
diagnostic position, and validate after parsing in reference-compatible order.
For example, `\1(a)` is invalid, `(?(1)a|b)(a)` is valid, and
`(?(x)a|b)(?P<x>a)` is invalid because the name is not defined when referenced.
The numeric backreference rules apply after escape parsing distinguishes group
references from octal character escapes.

Track the capture count at entry to lookbehind. References within it cannot use
open captures or captures defined within that same lookbehind. Earlier closed
captures may be referenced; width analysis separately checks the fixed-length
requirement. Preserve the enclosing lookbehind restriction when parsing nested
bodies. After reference resolution, backreference and conditional nodes contain
numeric capture indices; names are metadata and do not require matcher-side
lookup.

### Width Analysis

Width analysis counts how many subject elements a piece of regex can consume
without executing it. For example, `abc` consumes exactly three, `a|bc`
consumes one or two, `a*` consumes zero or more, and `(?=abc)` consumes zero
because it only checks ahead. Widths count Unicode code points or bytes,
independent of subject storage width; ignorecase does not expand their length.

Compute and cache two 64-bit bounds per node in a parallel vector indexed by
`NodeId`, with capture-body widths also available in the group table:

```cpp
struct Width
{
    uint64_t minimum;
    uint64_t maximum;
};

constexpr uint64_t WidthLimit = uint64_t{INT64_MAX} + 1;
```

| Node | Width rule |
|---|---|
| Empty sequence, anchor | `[0, 0]` |
| Literal, class, dot | `[1, 1]` |
| Sequence | Sum child minima and maxima |
| Alternative | Smallest branch minimum, largest branch maximum |
| Group, atomic group | Body width |
| Assertion | `[0, 0]`; analyze its body separately |
| Backreference | Referenced capture's width |
| Conditional | Combine yes/no branch widths like alternatives |
| Repeat | Multiply body bounds by repetition bounds |

An omitted conditional else branch has width `[0, 0]`. Repeat preference
(greedy/lazy/possessive) does not affect width. An unbounded repeat has maximum
`WidthLimit` unless its body has maximum zero, in which case its maximum is
zero. Zero repetitions also have zero width even for a body whose bounds are
capped. Assertions' bodies still require analysis for their own validation.

Cap static-analysis additions and multiplications at `WidthLimit`, checking
before native arithmetic can overflow. Below the ceiling, preserve the
calculated values. Equal capped bounds do not prove a fixed width. No separate
exactness flags are needed for the selected consumers. This cap is for derived
analysis only; dynamic repeat counters remain exact and follow their separate
overflow rules. These bounds do not prove that a match is possible.

For lookbehind, check the body in this order:

1. Minimum above `UINT32_MAX`: `PatternError("looks too much behind")`.
2. Unequal minimum and maximum:
   `PatternError("look-behind requires fixed-width pattern")`.
3. Otherwise, encode the fixed width as the lookbehind distance.

This ordering follows the reference compiler; both diagnostics have no public
pattern/position in local CPython 3.14.5 probes. A capped minimum already fails
the first check, so capped equality cannot pass as a fixed distance. For
example, `(ab){2,4}` has width `[4, 8]`, `(ab)\1` has `[4, 4]`, and `(?=abc)de`
has `[2, 2]`. Group-reference legality is checked separately; using a capture's
width does not imply that the capture participates at runtime.

Use the root minimum for search's minimum-width rejection. A body with width
`[1, 1]` is not automatically eligible for a simple repeat: it must also be the
agreed side-effect-free one-character test, without capture/assertion effects.

## Shared Unicode Data

Use a shared generated Unicode database, independent of regex programs and
subject storage width. Pin the initial data to Unicode 16.0.0, matching the
CPython 3.14 compatibility target and CloverVM's existing generated numeric
parsing tables. Record source versions, checksums, and the exact CPython
reference revision; preserve applicable attribution. Check generated C++ data
into the repository so ordinary builds need neither Python nor downloads.
Generation must not silently use an arbitrary host Python's Unicode version.

The required data is:

| Data | Use |
|---|---|
| Decimal membership | Unicode `\d` / `\D` |
| Python whitespace membership | Unicode `\s` / `\S` |
| Python alphanumeric membership | Unicode `\w` / `\W`, adding underscore |
| CPython-compatible one-code-point lowercase mapping | Unicode-ignorecase backreferences |
| Regex ignorecase equivalence groups | Compile-time expansion of literals and ranges |

Word boundaries share the word predicate. Preserve the distinction between
literal/class ignorecase equivalence and backreference lowercase comparison; do
not replace either with full string case folding. Generate compiler-only
equivalence-group data alongside runtime properties and mappings, with
CPython's regex-specific case fixes included in the reference behavior.

The Unicode layer accepts `uint32_t` code points and depends on neither
`String` nor regex execution state. Tables are static, read-only, and shared by
all compiled patterns. Use direct ASCII lookup or explicit fast paths, with the
shared membership bitmaps below for other code points. Keep lowercase mappings
in their separate delta table below. Equivalence groups live in separate
compact compiler-use arrays; compiled programs contain the expanded classes
rather than private copies of Unicode tables. Existing string methods can
migrate to the shared layer separately; current platform `isw*`/`tow*` calls
are not an authority for fixed Unicode regex semantics.

### Standard Unicode Class Bitmaps

Store three positive membership sets: decimal, Python whitespace, and Python
word (alphanumeric plus underscore). Complements reuse these sets with
negation; there are no separate tables for `\D`, `\S`, or `\W`. Word boundaries
use the same word set. Do not use per-code-point property-record indices for
these membership tests.

Choose a fixed compile-time page size of 10 code-point bits: 1,024 code points
per page. Each class has a directory of 1,088 `uint32_t` bitmap-word offsets.
The three directories reference one shared pool of deduplicated bitmap pages,
including shared empty and full pages. Each page contains sixteen `uint64_t`
words (128 bytes). The lookup for a selected class directory is:

```cpp
constexpr uint32_t UnicodeClassPageBits = 10;
constexpr uint32_t UnicodeClassPageMask = (1u << UnicodeClassPageBits) - 1;

uint32_t offset = directory[ch >> UnicodeClassPageBits];
uint32_t within_page = ch & UnicodeClassPageMask;
return (bitmap_words[offset + (within_page >> 6)]
        & (uint64_t{1} << (within_page & 63))) != 0;
```

The caller supplies a valid Python code point, including lone surrogates;
surrogates belong to none of the three positive sets. Offsets count `uint64_t`
elements. These static Unicode class pages are distinct from the 256-code-point
pages used by per-pattern explicit character classes.

A local Unicode 16.0.0 generation probe measured 89 unique 1,024-bit pages
across the three sets: 13,056 directory bytes plus 11,392 bitmap bytes,
totaling 24,448 bytes, excluding any separately chosen ASCII fast-path data.
The 2,048-code-point option measured 22,912 bytes; choose 1,024 code points as
the agreed size tradeoff. This is a fixed format choice, not automatic
page-size selection by the generator or runtime. Case mappings and compiler
equivalence groups are excluded from these measurements and have separate
layout decisions.

Unicode patterns use these fixed rules independently of process locale. Bytes
patterns use ASCII by default. Bytes `LOCALE` uses matching-time locale word
classification and case mappings, kept separate from the Unicode database;
bytes digit and whitespace predicates remain ASCII even under `LOCALE`. Unicode
patterns reject `LOCALE`. An explicit Unicode-ignorecase class such as `[a-z]`
uses the tables during compilation to expand membership.

### Unicode Identifier Validation

Within regex parsing, identifier validation checks group names and named
references, not subject text. For example, `(?P<word>...)` and `(?P<名前>...)`
use valid identifiers; names such as `123name` or `hello-world` are invalid.
Reject empty names. The first character must have Unicode `XID_Start` or be
underscore; remaining characters must have `XID_Continue`. Keywords such as
`class` are allowed as regex group names. Do not normalize or case-fold names.

Provide shared native predicates:

```cpp
bool is_identifier_start(uint32_t ch);    // XID_Start or '_'.
bool is_identifier_continue(uint32_t ch); // XID_Continue.
```

A whole-name validator checks the first character separately, then scans the
remainder. ASCII uses direct letter/digit/underscore checks. Unicode-pattern
names use these rules even under ASCII matching flags; bytes-pattern names
remain ASCII-only. Combining marks can be valid continuation characters,
whereas superscript `²` matches Unicode regex word membership but is not an
identifier character. Do not use `\w` as the identifier predicate.

Generate two additional positive membership sets, `XID_Start` and
`XID_Continue`, from the same pinned Unicode 16.0.0 source. Use the existing
ten-bit page layout and share deduplicated bitmap pages across these sets and
the three standard regex classes. The underscore exception for starting a
Python identifier belongs in the helper, not the raw `XID_Start` set.

A local Unicode 16.0.0 probe measured 153 unique pages across all five sets:
21,760 directory bytes plus 19,584 bitmap bytes, totaling 41,344 bytes. This
adds 16,896 bytes to the three-set standard-class layout. Generate and validate
against the pinned properties, not an arbitrary host Unicode version.

The helpers are shared infrastructure for regex group names, Python
tokenization, and `str.isidentifier()`. The current tokenizer's CTRE `\w+`
name pattern can migrate in a separate correctness change. Python source-name
normalization remains a separate compiler concern and does not apply to regex
group names.

### Unicode Regex Lowercase Mapping

For Unicode-ignorecase backreferences, compare characters for exact equality
first. Unequal ASCII characters use direct ASCII lowercasing; non-ASCII
characters use the shared CPython-compatible one-code-point lowercase mapping.
There is no normalization or multi-character expansion.

Store indices into a small table of signed lowercase deltas, with deduplicated
pages of byte-sized indices:

```cpp
uint32_t lowercase_page_offsets[];
uint8_t lowercase_delta_indices[];
int32_t lowercase_deltas[];
```

Choose a fixed compile-time split of nine bits: 512 code points per page.
Directory offsets count elements of `lowercase_delta_indices`; each page has
512 byte-sized indices. Lookup is:

```cpp
constexpr uint32_t LowercasePageBits = 9;
constexpr uint32_t LowercasePageMask = (1u << LowercasePageBits) - 1;

uint32_t offset = lowercase_page_offsets[ch >> LowercasePageBits];
uint8_t index = lowercase_delta_indices[offset + (ch & LowercasePageMask)];
return uint32_t(int32_t(ch) + lowercase_deltas[index]);
```

The generator validates the code-point bounds, signed arithmetic, and that the
number of distinct deltas fits an eight-bit index. Unchanged characters select
delta zero, and all-unchanged pages share storage. This table is separate from
the ten-bit standard-class bitmap tables and compiler equivalence groups.

A local CPython 3.14.5 / Unicode 16.0.0 probe using `_sre.unicode_tolower`
found 1,460 changed code points but only 84 distinct deltas including zero. The
nine-bit layout has 2,176 directory entries, 18 unique index pages, and 84
signed deltas: 8,704 + 9,216 + 336 = 18,256 bytes. Measured totals for eight-,
ten-, and eleven-bit layouts were 24,144, 20,048, and 29,136 bytes,
respectively. Select nine bits as a fixed format choice; generation does not
automatically select a split.

### Compiler Ignorecase Equivalence Groups

An equivalence group contains the characters matched by a Unicode-ignorecase
literal, for example `{a, A}`, `{k, K, K}`, or `{i, I, İ, ı}`. These shared
tables are compiler input, not another runtime membership representation. The
compiler expands literals and explicit classes using the groups, then selects
the existing range, bitmap, or paged class representation.

Generate the groups and lowercase delta tables together from the same pinned
Unicode and CPython reference data. Combine equal regex lowercase results with
CPython's extra literal/class case relationships. Validate that every index key
and member agrees with those rules; the two generated outputs must not drift
apart. Backreferences continue to use only lowercase comparison, not these
additional equivalences.

Use a sorted sparse index keyed by regex lowercase code point, group records,
and a flat member pool:

```cpp
struct CaseGroupIndex
{
    uint32_t lowercase_key;
    uint32_t group_index;
};

struct CaseGroup
{
    uint32_t members_offset;
    uint32_t members_count;
};

CaseGroupIndex case_group_index[];
CaseGroup case_groups[];
uint32_t case_group_members[];
```

To expand a literal, lowercase it with the shared mapping, then binary-search
the index. A hit selects the group's members; a miss retains the original
literal as a singleton. Different lowercase keys can reference the same group:
both `i` and `ı` select `{i, I, İ, ı}`. Store only nontrivial groups.

For an ignorecase range, retain the original range and add each group that
intersects it. Scan the grouped members rather than every code point in a large
range. Normalize duplicates and adjacent ranges before choosing the compiled
class representation. Apply outer negation only after expansion.
ASCII-ignorecase runs retain their existing lowering rules.

A local CPython 3.14.5 / Unicode 16.0.0 probe combining `_sre.unicode_tolower`
and `re._casefix._EXTRA_CASES` found 1,454 nontrivial groups, 2,940 members,
and 1,480 lowercase index keys. Groups contain two to four members in this data
version; store explicit counts rather than assuming that maximum permanently.
The three arrays occupy 35,232 bytes with the specified 32-bit fields. These
are shared compiler tables, not per-pattern allocations or runtime
case-conversion requirements.

### Shared Unicode Name Lookup

Adapt CPython 3.14's generated Unicode name representation in CloverVM's shared
native Unicode layer. Ordinary names use the packed DAWG (compressed word
graph) representation from `unicodename_db.h`, with CPython's dedicated
handling for algorithmic names such as Hangul syllables and CJK unified
ideographs. Pin the adapted source/generator revision and input Unicode
version, retain applicable licenses and attribution, and check generated data
into the repository under the same provenance rules as the other Unicode data.

Share native lookup helpers and static read-only data among the regex parser,
Python string escape handling, and the future `unicodedata` module. Support
both name-to-code-point and code-point-to-name operations through this layer,
preserving the reference representation's alias and named-sequence handling
where applicable. These are separate data from hot matching property tables;
there is no per-pattern name database.

CPython's regex parser calls `unicodedata.lookup`, while its core string escape
decoder accesses the native lookup through the module's private capsule API.
CloverVM's internal callers use shared C++ helpers directly; importing a Python
module or reproducing the capsule mechanism is unnecessary for native sharing.
Keep each caller's error policy at its own boundary.

For regex `\N{name}`, resolve the name while parsing and produce an ordinary
literal node. Names are case-insensitive according to the reference lookup
rules. Unknown names raise positioned `PatternError`; bytes patterns reject
`\N` as an invalid escape. Regex escapes require exactly one character, even
though the shared name service must also accommodate named sequences for
callers that support them. No special name-lookup instruction runs during
matching. Concrete helper signatures and caller adapters remain implementation
work within this shared-layer decision.

## Compiled Program And Builder

The compiled program owns its instruction stream, literal pool, typed class
tables, and class payload pools directly. Follow `CodeObject`'s use of owning
vectors, with private storage and read-only public accessors for the regex
program. There is no non-owning `ProgramArray` member and no packed allocation:
each vector owns and releases its own allocation through normal RAII.

Immutability is an API contract after construction. Members and their backing
pointers can remain mutable C++ types internally; public accessors expose only
const elements. The object itself does not need to be C++ `const`. A returned
span borrows storage owned by the program and cannot outlive it.

The ownership/access pattern is:

```cpp
template<typename Literal>
class ProgramBuilder;

template<typename Literal>
class CompiledProgram
{
public:
    CompiledProgram(const CompiledProgram&) = delete;
    CompiledProgram& operator=(const CompiledProgram&) = delete;
    CompiledProgram(CompiledProgram&&) = delete;
    CompiledProgram& operator=(CompiledProgram&&) = delete;

    std::span<const uint32_t> instructions() const { return instructions_; }
    std::span<const Literal> literals() const { return literals_; }

private:
    friend class ProgramBuilder<Literal>;
    CompiledProgram() = default;

    std::vector<uint32_t> instructions_;
    std::vector<Literal> literals_;
    std::vector<BitmapClass> bitmap_classes_;
    std::vector<RangeClass> range_classes_;
    std::vector<PagedClass> paged_classes_;
    // Other payload pools and metadata follow the same owning pattern.
};

template<typename Literal>
class ProgramBuilder
{
public:
    ProgramBuilder(const ProgramBuilder&) = delete;
    ProgramBuilder& operator=(const ProgramBuilder&) = delete;
    ProgramBuilder(ProgramBuilder&&) = delete;
    ProgramBuilder& operator=(ProgramBuilder&&) = delete;

    [[nodiscard]] Result<std::unique_ptr<CompiledProgram<Literal>>, ProgramError>
    freeze() &&;

private:
    std::unique_ptr<CompiledProgram<Literal>> program_;
    // Compile-only pool interning and construction state.
};
```

Use the native `Result` type for builder construction errors.
The snippets omit builder setup constructors. Neither the program object nor
the builder can be copied, assigned, or moved. Transfer a frozen program by
moving its `unique_ptr`, not its contents. A builder stays at a stable address
and outlives its jump targets and structured helpers; those helpers are also
noncopyable and nonmovable. `std::move(builder).freeze()` selects the consuming
member without moving the builder object. These restrictions prevent implicit
special members from mutating published storage or invalidating helper pointers.
Instantiate the literal storage with `uint8_t` for bytes and `uint32_t` for
Unicode. The remaining owned payloads include inclusive 32-bit range endpoint
pairs, the shared `uint64_t` bitmap-word pool, and the `uint32_t` page
directory. Typed class/payload accessors follow the same const-element pattern.
Program metadata belongs to the program; mutable compiler bookkeeping belongs
to the builder, and execution scratch belongs to each match invocation.

The friend builder creates and fills an unpublished program. Its consuming
`freeze()` requires explicit root completion and resolved jump-target
relocations, finalizes pools and metadata, checks format limits, and runs
structural verification in debug builds before
transferring ownership of the completed object. On failure it publishes no
program. After success, the builder retains no writable aliases into the
program, and execution never mutates its storage. Freezing does not copy or
pack the vectors into another allocation.

Native container sizes remain host-sized internally; conversions into the
engine's 32-bit counts and indices must be checked against the agreed limits.
The Python `Pattern` object owns the completed program, as specified below; the
unique pointer above expresses the builder's ownership transfer.

All control-flow targets occupy full 32-bit operand words. Instruction sizes
are known when emitted: the builder appends words directly, labels bind to the
current word index, and unresolved references record the operand word to patch.
Binding fills those operands without inserting words or relocating later
instructions. No layout relaxation or pending linear instruction list is
needed. Compact/wide selection for literal, pool, capture, and simple-repeat
operands uses values known before emitting that instruction; pool indices stay
stable as storage grows.

### Program Metadata

The compiled program owns the immutable pattern definition. The Python
`Pattern` owns the program and forwards metadata access to it, avoiding a
second copy of flags or group definitions. The original Python pattern string
stays on `Pattern`; the standalone C++ program contains no VM handles or managed
source-string ownership.

```cpp
struct ProgramMetadata
{
    uint32_t capture_count; // Excludes whole-match group zero.
    uint32_t global_flags;  // Final effective global flag bits.
    SearchInfo search;
};
```

The literal type already distinguishes bytes and Unicode programs; no extra
category field is needed. Capture count sizes and checks marks and references.
Final global flags include implicit Unicode mode where applicable. Store their
32-bit bit pattern, but expose `Pattern.flags` using its signed `int32_t`
interpretation. Check the reference signed-C-int range at the same conversion
stage as CPython, not as an eager flag-validation pass. For example,
`compile("a", -1024).flags` is `-992`, bytes retain `-1024`, and `1 << 31`
raises `OverflowError` when converted for compilation. Preserve Python bitmask
processing and its error precedence before that conversion. Scoped flags are
lowered into instructions/classes; the matcher does not switch behavior using
the global metadata field.

Store only named captures, in capture-number order, with owned code-point
storage:

```cpp
struct GroupName
{
    uint32_t group; // Zero-based capture index.
    uint32_t name_offset;
    uint32_t name_length;
};

// Private CompiledProgram members:
ProgramMetadata metadata_;
std::vector<GroupName> group_names_;
std::vector<uint32_t> group_name_codepoints_;
std::vector<uint32_t> group_name_lookup_order_;
std::vector<uint32_t> prefix_overlap_;
```

Names use Unicode code points for both program categories; bytes-pattern names
are restricted to ASCII. A separate index vector sorted by name supports
binary search while the primary table retains capture order. These tables
support `groupindex`, named access, `groupdict`, and `lastgroup` without storing
metadata for each unnamed group.

`SearchInfo` is specified under Search Metadata. Prefix slices reference the
ordinary literal pool; the program owns the overlap table separately. Default
search metadata has minimum width zero, no filter, and no anchoring
restriction, giving builder-generated tests a correct unoptimized default.

```cpp
// ProgramBuilder<Literal> methods:
BuildResult<void> set_capture_count(uint32_t count);
BuildResult<void> set_global_flags(uint32_t flags);
BuildResult<void> add_group_name(uint32_t group,
                                 std::span<const uint32_t> name);
BuildResult<void> set_search_info(const SearchInfo& info,
                                  std::span<const uint32_t> prefix_overlap);
```

Supply capture count before instruction emission so capture-related methods
can check indices. Copy names and overlap data into owned storage before
freezing. `freeze()` completes name lookup ordering and checks metadata
references and limits. Public program accessors expose const data only.

### Program Error Payload

Use one allocation-free error payload for construction, decoding, and debug
verification, independent of VM exception state:

```cpp
enum class ProgramErrorKind : uint8_t
{
    LimitExceeded,
    OutOfMemory,
    InvalidProgram,
};

struct ProgramError
{
    ProgramErrorKind kind;
    std::optional<uint32_t> word_offset;
    const char* message; // Static string literal.
};

template<typename T>
using BuildResult = Result<T, ProgramError>;
```

`LimitExceeded` covers instruction/pool format ceilings and maps to
`OverflowError` at the future binding. Allocation failure maps through
`OutOfMemory` to `MemoryError`. Internal construction/encoding defects such as
unresolved targets, malformed operands, or incorrect nesting use
`InvalidProgram`, mapping to `RuntimeError`.

The optional offset identifies an offending instruction or operand word where
available; leave it absent for failures such as pool allocation. Messages have
static lifetime, so constructing an error never allocates. Add offset formatting
only when displaying diagnostics. Construction boundaries translate
`std::bad_alloc` into `OutOfMemory`; ordinary typed propagation needs no pending
VM exception or cancellation machinery. Do not add offending-value fields,
nested errors, or dynamically formatted messages without a concrete need.

Keep these internal program errors separate from future parser/compiler
syntax diagnostics, which carry source positions and become `PatternError`.

### RAII Jump Targets

Follow `CodeObjectBuilder`'s target-local relocation ownership. Each stack-scoped
target owns its unresolved relocations and is associated with one builder.
Explicit `resolve()` binds the current instruction-word offset and patches
existing operand words. Destruction releases the helper's storage without
asserting completion or automatically binding it. Do not introduce a label-ID
table or global fixup list.

```cpp
template<typename Literal>
class JumpTarget
{
public:
    explicit JumpTarget(ProgramBuilder<Literal>& builder);
    JumpTarget(const JumpTarget&) = delete;
    JumpTarget& operator=(const JumpTarget&) = delete;
    ~JumpTarget();

    BuildResult<void> resolve();

private:
    friend class ProgramBuilder<Literal>;
    BuildResult<void> add_relocation(uint32_t operand_word);

    ProgramBuilder<Literal>* builder_;
    uint32_t scope_id_; // Construction scope; not stored in the program.
    std::optional<uint32_t> target_;
    absl::InlinedVector<uint32_t, 2> unresolved_operands_;
};
```

`ProgramBuilder<Literal>::emit_jump` accepts an ordinary
`JumpTarget<Literal>&`; conditional group-existence transfers use the same
scope-checked relocation mechanism. Before binding, references append
operand-word offsets to the target. `resolve()` binds once, patches them, and
clears the list. References added after binding are patched immediately. Check
builder and scope identity; no operation changes instruction size.

```cpp
JumpTarget<Literal> done(builder);
CL_TRY(builder.emit_jump(done));
// Emit ordinary instructions in this same scope.
CL_TRY(done.resolve());
// Emit the destination instruction.
```

Structural branch emission is helper-controlled. Its low-level `emit_branch`
uses target-local fixups with the owning construct's checked roles, rather than
exposing cross-scope targets to ordinary compiler calls. The alternation helper
owns header chaining, tail jumps, and the terminator; no global fixup table is
introduced.

Regex construction helpers have no destructor-completion assertions. Ordinary
RAII releases helper storage and the builder's unfinished program on any return
path; no cancellation API, cancellation state machine, or pending-exception
inspection is required for cleanup. This applies to scoped target and
repeat/region helpers as well as the builder itself.

Keep publication checks explicit: the builder tracks outstanding relocations
and open structured emission, and `freeze()` rejects incomplete construction,
including references whose target was destroyed without resolution. It must
not publish placeholder target words or a program lacking root completion.
These checks do not depend on helper lifetime assertions. Other construction
errors propagate normally; the caller discards the unfinished builder after
failure.

### Explicit Root Completion

The compiler/caller explicitly emits the final root `SUCCESS` through
`BuildResult<void> emit_success()`. `freeze()` never appends it or silently
repairs unfinished code. The empty regex has a one-word `SUCCESS` program;
an empty instruction stream is invalid.

Root `emit_success()` requires all structured helpers to be closed and ends
the instruction-emission phase. No instructions or label bindings may follow
it. Resolve labels for the root ending immediately before emitting it, so they
identify that instruction rather than a one-past-end location. Metadata/pool
finalization retains its existing checked contracts.

```cpp
// Emit the root body and close its structured helpers.
CL_TRY(builder.emit_success());
auto program = CL_TRY(std::move(builder).freeze());
```

Local assertion/atomic/possessive `SUCCESS` instructions are emitted internally
by their owning helper, do not seal the root, and do not satisfy its completion
requirement. Production `freeze()` checks that explicit root completion was
emitted, in addition to unresolved/open-construct and resource checks. The debug
verifier independently requires a nonempty stream whose final instruction is
root-owned `SUCCESS`; reject misplaced completion, trailing words, and ordinary
fallthrough beyond the program or across a scope boundary. An always-failing
pattern can contain `FAILURE` but still has the final root `SUCCESS` in its
canonical compiled layout.

### Label Scopes And Structural Targets

Each ordinary `JumpTarget` records the builder's current construction scope
when created. Its references must originate in that same scope, and `resolve()`
must bind within it. Check builder identity as well as scope identity. This
permits forward references without storing source-scope information in every
relocation or introducing a global fixup table. Helpers and targets remain
noncopyable/nonmovable and scopes have distinct builder-local 32-bit IDs.

Create separate scopes for the root program, each ordinary repeat body, each
assertion/atomic/possessive local child body, and each alternation arm.
Ordinary jumps and conditional transfers stay within their scope. Structured
helpers own the specific cross-scope fixups:

| Structural target role | Permitted owner/use |
|---|---|
| Next alternative header | The owning alternation's header chain |
| Shared alternative tail | The owning alternation's arm-ending jumps |
| Repeat ending | Its ordinary `REPEAT` entry |
| Local-body continuation | Its assertion, atomic, or possessive entry |

These targets/fixups are helper-controlled and checked against that helper's
construct kind and boundaries. Do not expose a general ignore-scope option.
An alternation's `next_alternative()` closes the current arm scope and opens
the next; `close()` returns to the surrounding scope before the shared tail.
Other helpers similarly restore their surrounding construction scope after
emitting the owned ending. Scope IDs and roles remain compile-only metadata.
The debug verifier reconstructs independent scope/role assignments from words;
the immutable program contains no extra scope table.

### Structured Alternation, Repeat, And Region Emission

Follow the scoped-helper style of `ExceptionTableRangeBuilder`, with explicit
fallible `begin()` and `close()` operations. Helper destructors release storage
without closing constructs or asserting completion. The builder owns an inline
vector of open constructs, recording kind and entry word offset. Each helper
remembers its entry offset; closing must match the innermost open construct,
then remove it after successful emission. `freeze()` rejects any remaining open
construct, including one whose helper has already been destroyed. This is
compiler bookkeeping, not the executor's runtime context stack.

Alternatives use:

```cpp
AlternationBuilder<Literal> alternatives(builder);
CL_TRY(alternatives.begin());
// Emit first arm.
CL_TRY(alternatives.next_alternative());
// Emit second arm.
CL_TRY(alternatives.close());
// Emit shared tail.
```

Beginning emits the first `BRANCH` header. `next_alternative()` emits the
previous arm's shared-tail jump, resolves its next-header target, and emits
the next header. Closing emits the final arm's tail jump, resolves its
next-header target to the terminating `FAILURE`, emits that terminator, and
resolves the shared tail. The helper owns these fixups and the complete chain.
It follows the same explicit begin/close, nesting, lifetime, and freeze rules
as other structured helpers.

General repeats use:

```cpp
RepeatBuilder<Literal> repeat(builder);
CL_TRY(repeat.begin(RepeatMode::Greedy, bounds));
// Emit the repeated body.
CL_TRY(repeat.close());
```

The helper owns the repeat-ending `JumpTarget`. Beginning emits `REPEAT`,
referencing that target and the supplied bounds. Closing resolves the target
to the appropriate `REPEAT_END_GREEDY` or `REPEAT_END_LAZY` instruction and
emits that ending. The tail follows immediately; there is no `REPEAT_EXIT`.
Use `RepeatMode` for greedy, lazy, and possessive modes, shared with simple
repetition. For possessive mode, the same helper instead emits
`REPEAT_POSSESSIVE` with a tail target, closes with local `SUCCESS`, and resolves
the tail immediately afterward. It never lowers possessive mode to an ordinary
repeat inside an atomic wrapper.

Assertions and atomic groups share a region helper:

```cpp
RegionBuilder<Literal> assertion(builder);
CL_TRY(assertion.begin_assertion(AssertionPolarity::Positive,
                               lookbehind_distance));
// Emit assertion body.
CL_TRY(assertion.close());

RegionBuilder<Literal> atomic(builder);
CL_TRY(atomic.begin_atomic());
// Emit atomic body.
CL_TRY(atomic.close());
```

Beginning emits `ASSERT`, `ASSERT_NOT`, or `ATOMIC_GROUP`, with a helper-owned
continuation target. Assertion distance zero selects lookahead. Closing emits
local `SUCCESS`, then resolves the continuation to the next instruction boundary.
The compiler supplies the body and analysis-derived lookbehind distance.

Simple one-character repeats are emitted in one call:

```cpp
BuildResult<void> emit_repeat_one(RepeatMode mode, RepeatBounds bounds,
                                const CharacterTest& test);
```

`RepeatMode` includes greedy, lazy, and possessive. `CharacterTest` represents
only the eligible side-effect-free tests, excluding captures, backreferences,
assertions, literal runs, and arbitrary instruction sequences. Emitting the
header and complete test together prevents callers from inserting operations
or binding labels within the embedded operand. Select header/test compact or
wide forms from known operands before appending words. These helpers own
encoding, endings, relocations, and nesting; the compiler owns body lowering
and construct selection.

### Pool APIs

Pool APIs copy caller input into program-owned vectors and return small typed
references. Input spans are borrowed only for the call; returned offsets and
indices remain stable as vectors grow and are local to the originating builder.

```cpp
struct LiteralSlice
{
    uint32_t offset;
    uint32_t length;
};

struct BitmapClassId { uint32_t index; };
struct RangeClassId { uint32_t index; };
struct PagedClassId { uint32_t index; };

struct CodePointRange
{
    uint32_t first;
    uint32_t last; // Inclusive.
};

using Bitmap256 = std::array<uint64_t, 4>;

// ProgramBuilder<Literal> methods:
BuildResult<LiteralSlice> add_literals(std::span<const Literal> text);
BuildResult<BitmapClassId> add_bitmap_class(const Bitmap256& bitmap,
                                          ClassMetadata metadata);
BuildResult<RangeClassId> add_range_class(std::span<const CodePointRange> ranges,
                                        ClassMetadata metadata);
BuildResult<PagedClassId> add_paged_class(std::span<const CodePointRange> ranges,
                                        ClassMetadata metadata);
```

`ClassMetadata` contains the agreed outer-negation and category bits, not regex
flags. Range input is sorted, disjoint, and merged after fixed case expansion.
The compiler expands and normalizes membership and selects a representation
using the agreed intrinsic-cost policy. The builder stores that representation,
constructs page directories, deduplicates bitmap pages, and assigns offsets.
`add_paged_class()` accepts ranges rather than requiring caller-created page
directories or payload-pool offsets. Class factories populate storage records
internally.

Use returned class references to construct the same typed `CharacterTest` used
for ordinary test emission and embedded simple repeats:

```cpp
auto cls = CL_TRY(builder.add_bitmap_class(bitmap, metadata));
auto test = CharacterTest::in_bitmap(cls);
CL_TRY(builder.emit_test(test));
CL_TRY(builder.emit_repeat_one(mode, bounds, test));
```

Literal and range pools append. Intern identical fixed bitmaps and Unicode
pages in the common four-`uint64_t` payload representation. Every addition
checks arithmetic and 32-bit format limits. `freeze()` transfers owning vectors
without rearranging their contents and discards compile-only interning tables.
No mutable pool accessors or separate allocation per class are needed.
`emit_literal_run(text)` may remain a convenience that adds literals and emits
the appropriate run form.

### Inspection Decoder

The decoder remains inspection-only. Shared numeric opcode definitions and
operand metadata describe the settled context-model layouts below.

Use a stateless decoder for debug verification, disassembly, and tests:

```cpp
struct DecodedCharacterTest
{
    Opcode opcode;
    uint32_t operand; // Zero for ANY / ANY_ALL.
};

struct DecodedInstruction
{
    Opcode opcode; // Actual encoding, including any _WIDE form.
    uint32_t pc;
    uint32_t next_pc;
    std::array<uint32_t, 3> operands;
    std::optional<DecodedCharacterTest> repeat_test;
};

Result<DecodedInstruction, ProgramError>
decode_instruction(std::span<const uint32_t> words, uint32_t pc);
```

The returned value owns its decoded fields and requires no allocation or
literal-type template. Normalize compact and wide operands into ordinary
32-bit values; retain the actual opcode so inspection identifies the encoded
form. Shared opcode metadata supplies operand count and meaning, and unused
operand slots are zero.

| Instruction | Normalized operands |
|---|---|
| Literal | Character |
| Literal run | Pool offset, length |
| Class test | Class index |
| `BRANCH`, `JUMP` | Absolute next-alternative/tail target respectively |
| `GROUPREF_EXISTS` | Group index, false target |
| Assertion entry | Continuation, lookbehind distance |
| General repeat entry | Repeat-ending target, minimum, maximum |
| General possessive entry | Tail target, minimum, maximum |
| Simple repeat | Minimum, maximum, plus separate `repeat_test` |

A simple repeat is one complete decoded instruction: the embedded test is
returned in `repeat_test`, with its actual opcode and normalized operand, and
`next_pc` skips its full encoding. Do not expose that test as another top-level
instruction. Three operand slots suffice for the selected instruction set.

Check opcode/encoding validity, available operand words, reserved bits, and
embedded test eligibility/completeness. Return the shared `ProgramError` with
the offending word offset. Decoding does not inspect pools or build a control-flow
graph. The whole-program verifier checks pool/capture bounds, target boundaries
from a complete walk, and structural nesting. Disassembly resolves pool contents
when printing. Callers walk from `pc=0` using each result's `next_pc` until the
stream ends.

The matching interpreter does not use this decoder or construct decoded
objects. It dispatches directly on encoded words. Share opcode definitions and
encoding constants between execution and inspection to keep them consistent.

### Construction Guarantees And Debug Verification

Preserve the publication and error boundaries below. Validate the context
model's structured execution scopes and completion rules; the old undo-stack
protocol is not a verifier invariant. Enforce legal target scopes during
construction and independently check them in the debug verifier. The label
scope and helper-owned structural-target rules are specified above; the
independent reconstruction/checking algorithm follows below.

Compiled programs are internal compiler output, not externally supplied
bytecode. Make them correct by construction through typed emission helpers,
labels/fixups, and structured alternation/repeat/region emission. Do not require
an independent full structural verifier in production `freeze()`.

Always check arithmetic, format/resource limits, and bounds when creating
pool references, indices, slices, and fixups. Resolve every label before
publication and preserve the builder's ownership-transfer guarantees.
Python-visible syntax, flag, group-reference, and template validation remain
required independently of build mode.

Use a full verifier in debug builds and tests to catch codegen bugs: malformed
instruction extents or reserved fields, invalid operands and targets, jumps
into embedded simple-repeat tests, invalid class metadata/payloads, and
incorrect repeat/region nesting or endings. Share instruction-format
definitions with decoding and disassembly. Debug `freeze()` verifies the
completed program before publication; verifier failures are internal errors,
not invalid user-pattern diagnostics. Detected invalid internal bytecode/state
uses the existing `RuntimeError` mapping.

| Construct | Builder emits | Debug verifier establishes |
|---|---|---|
| Alternation | Header chain, arm-ending jumps, terminating `FAILURE`, shared tail | Finite chain, valid header roles/targets, and consistent shared tail |
| Ordinary repeat | `REPEAT`, body, mode-specific repeat ending | Entry targets its own ending; no bypass of setup or completion |
| Possessive repeat | Dedicated entry, body, local `SUCCESS`, tail | Correct local body boundary and continuation; no ordinary repeat ending |
| Assertion/atomic | Entry, body, local `SUCCESS`, continuation | Valid child body and continuation; no ordinary transfer into or out of child |
| Simple repeat | Header plus complete eligible test | Embedded extent/eligibility, with no targets inside it |

Instruction boundaries alone do not make a target legal. Reject ordinary
transfers into repeat bodies without setup, out of repeats while bypassing
their endings, into/out of local child bodies, through later headers of an
existing alternative chain, or into operands/embedded tests. Helper-owned
structural fixups express the intended entries, endings, and shared-tail
transfers. Scope/role bookkeeping belongs to construction and debug checking,
not an additional frozen-program table.

Check `SUCCESS` in its execution scope: root completion and local child-body
completion have different owners. A repeat introduced in that computation
must reach its ending rather than be silently completed by `SUCCESS`. A local
assertion inside an outer repeat can still finish with its own `SUCCESS`;
the enclosing repeat belongs to its parent computation. These are structural
contracts, not simulation of runtime resume states or proof of capture results.

Verification checks the compiled structure, not semantic equivalence to the
source regex. AST analysis and differential tests establish properties such as
the necessity of a search prefix and correct capture restoration. Runtime
subject-bound checks, allocation failures, and repeat-counter exhaustion retain
their existing handling.

### Verifier Scope Reconstruction

Use three passes, independently of builder scope IDs or fixup metadata. All
maps, scope records, and work items are verifier scratch, not frozen-program
data or interpreter inputs.

First decode the entire word stream. Record valid instruction starts and
extents, rejecting malformed/truncated encodings and reserved fields. Embedded
simple-repeat tests remain operands and are never targetable instruction
starts. Check all target arithmetic before indexing.

Second reconstruct structured regions using an explicit work stack. Each item
has a bounded range, scope, completion owner, and expected ending. Assign an
instruction's scope/role while walking its range; skip child ranges in the
surrounding walk rather than rescanning them for each ancestor.

| Encountered construct | Reconstruction |
|---|---|
| Ordinary `REPEAT` | Its target must identify its own greedy/lazy ending. Walk the body in a new repeat scope; resume the surrounding walk after that ending. |
| Assertion, atomic, possessive entry | Continuation identifies the tail. Its preceding complete instruction must be local `SUCCESS`. Walk the body in a new child scope, then resume at the tail. |
| First `BRANCH` | Follow the chain, establish arm ranges and shared tail, and walk each arm in a distinct scope. |
| Simple repeat | Validate its eligible embedded test and advance over the complete instruction. |

Nested constructs must fit wholly within their enclosing ranges. Reject
crossing/overlapping constructs and stray repeat endings. Recognizing an ending
requires an instruction-start/extent check, not finding an opcode-looking
operand word. Do not expose child instructions to the parent's ordinary walk.

For each alternative header, its next target must advance to another header
or the terminating `FAILURE`. The arm must end immediately before that next
header/terminator with a complete two-word `JUMP`. Every such arm-ending jump
must identify the same shared tail, immediately after the terminator. Check
the chain stays inside its enclosing range. Mark the first header as an entry,
subsequent headers as chain-only, and arm-ending jumps as structural transfers.
Infer these roles from layout, not a claim attached to a builder label.

Third check every transfer against the reconstructed scope/role map:

- Ordinary `JUMP` and the false transfer of `GROUPREF_EXISTS` require an
  instruction-start target in the source's scope that permits ordinary entry.
  Later headers and the terminator of an existing chain do not permit it.
- Branch links must identify their own next header/terminator, and arm-ending
  jumps their own shared tail.
- Repeat entries must target their own ending, and local-body entries their
  own continuation. A matching destination opcode in another construct is
  insufficient.

Completion ownership is distinct from lexical jump scope. Alternation arms
inherit their surrounding computation's completion owner; local assertion,
atomic, and possessive bodies establish a new one. Ordinary repeats introduced
inside a computation must finish through their own endings. Validate `SUCCESS`
against this structure: a nested assertion can finish locally inside an outer
repeat, while a success that improperly bypasses an ordinary repeat ending is
invalid. Do not simulate capture state or runtime resume identifiers.

Visit instructions and chain headers a bounded number of times and use checked
temporary storage growth. Scope reconstruction must not repeatedly scan whole
nested intervals. Return the existing allocation-free `ProgramError` payload
on malformed structure, index limits, or scratch-allocation failure. Apply the
explicit root-completion contract: the final instruction is root-owned
`SUCCESS`, local successes occur at their owned body endings, and ordinary
fallthrough stays within its scope and instruction stream. Structured entry,
ending, and arm-tail edges use their verified construct-specific rules instead
of ordinary fallthrough checks.

## Bytecode And Execution

### Instruction Words And Literals

The instruction stream consists of aligned 32-bit words, with an 8-bit opcode
in the low bits of the first word. Opcodes determine their instruction length
and the interpretation of the remaining bits or following words. The agreed
literal instructions are:

| Instruction | Encoding |
|---|---|
| `LITERAL` | One word: opcode in bits 0-7, immediate character in bits 8-31 |
| `LITERAL_RUN` | One word: opcode in bits 0-7, pool offset in bits 8-23, length in bits 24-31 |
| `LITERAL_RUN_WIDE` | Three words: opcode word with upper 24 bits zero; 32-bit pool offset; 32-bit length |
| `NOT_LITERAL` | One word: opcode in bits 0-7, excluded character in bits 8-31 |

Single-character literals use `LITERAL`. All Python Unicode code points,
including lone surrogates, fit in its 24-bit immediate; byte literals fit too.
Matching widens the subject element rather than truncating the literal.

`NOT_LITERAL` consumes exactly one subject character unequal to its immediate,
and fails at end of subject. One inline form is sufficient: 32-bit Unicode
storage contains code points no greater than `0x10FFFF`, which fit in 24 bits.
There is no out-of-line or negated-run form. A set of excluded characters uses
a negated class; a forbidden sequence uses a negative assertion. Fixed-rule
case-insensitive literals lower as described below.

Longer sequences use a separate typed literal pool: `uint8_t` elements for
bytes programs, `uint32_t` elements for Unicode programs. Offsets and lengths
count pool elements, not bytes. Use the compact run when the offset and length
fit their fields, and `LITERAL_RUN_WIDE` otherwise. Both operands in the wide
form are full 32-bit values; there is no 24-bit wide-run length limit. Both
forms remain subject to pool bounds and must preserve matching-rule and
semantic boundaries. A one-element run is representable but is unnecessary as a
fallback for valid Python character values.

Literal payloads do not contain managed object pointers or depend on subject
storage width. The run handler compares the referenced sequence without a
bytecode dispatch for every character. The compiler must preserve capture and
assertion boundaries and changes in matching rules when forming runs.

### Fixed-Rule Case-Insensitive Literals

Use `LITERAL_RUN_ASCII_IGNORECASE` and `LITERAL_RUN_ASCII_IGNORECASE_WIDE` for
runs whose complete per-position matching sets are ASCII letter pairs. These
have the same compact and wide operand layouts as `LITERAL_RUN` and
`LITERAL_RUN_WIDE`, respectively. Pool elements are lowercase ASCII letters;
every position matches exactly that letter or its uppercase partner. A
comparison can use `(subject_code_point | 0x20) == literal`. The literal must
be in `a`-`z`: this operation is not safe as a general case-insensitive
comparison for digits or punctuation.

The compiler selects the matching mode from effective scoped flags. For fixed
ASCII/Unicode rules, compute exceptional literal membership sets during
compilation and emit ordinary representation-specific class tests. A negated
literal can similarly become a negated class, expanding its case equivalents
before final negation. This does not require dedicated case-insensitive
singleton literal or negated-literal opcodes. Characters unaffected by the
selected case rules can remain ordinary literals/runs.

Under Unicode `IGNORECASE`, ASCII `i`, `k`, and `s` (and their uppercase forms)
have additional non-ASCII equivalents and must break the ASCII-ignorecase run:

| ASCII pair | Additional matching code points |
|---|---|
| `i` / `I` | `ı` (U+0131), `İ` (U+0130) |
| `k` / `K` | `K` (U+212A) |
| `s` / `S` | `ſ` (U+017F) |

Emit a class for each exceptional literal rather than add per-position
exception metadata or fallback lookups to the run handler. Other non-ASCII
literals use their compile-time matching sets as needed. Preserve Python's
actual regex relations, not general full-string case folding or locale-tailored
Unicode rules. Unicode regex literal matching is independent of the process
locale.

For bytes `IGNORECASE` without `LOCALE`, all ASCII letters, including `i`, `k`,
and `s`, can use this run. The same is true under Unicode `ASCII | IGNORECASE`.
The opcode can execute against any Unicode subject storage width where the
compiler has established that the pair is the full matching set.

Digits and punctuation break the pair run and use exact matching. Thus an
insensitive `abc123` is two runs:

```text
LITERAL_RUN_ASCII_IGNORECASE "abc"
LITERAL_RUN "123"
```

An insensitive Unicode `this` becomes an ASCII-ignorecase run for `th`, then
classes for `i` and `s`. Do not introduce a per-position folding-mask stream or
general sequence-of-classes instruction in this initial design. This keeps
acceptance and rejection cheap in the run loop; the dispatch/fragmentation cost
must still be measured. Singleton handling can use ordinary classes rather than
a new case-insensitive immediate-literal opcode.

Backreference case modes are specified below. Bytes `LOCALE` uses the locale at
match time, so its membership cannot be permanently expanded using the
compilation-time locale. Locale-sensitive class instructions are specified
below. Simple-repeat eligibility includes side-effect-free one-character class
tests emitted by this lowering, not the multi-character run instruction itself.

### Simple Instructions

`MARK` writes live state directly; capture preservation belongs to the
suspending handlers. `SUCCESS` completes the current computation; context
acceptance settings distinguish local-body completion from whole-match checks.

Each instruction below occupies one word, with an 8-bit opcode in bits 0-7 and
the indicated payload in bits 8-31.

| Instruction | 24-bit payload | Behavior |
|---|---|---|
| `SUCCESS` | Unused | Complete the current computation, applying match-mode/progress requirements only when its context checks acceptance |
| `FAILURE` | Unused | Fail the current path and backtrack if an alternative remains |
| `ANY` | Unused | Consume one character except newline |
| `ANY_ALL` | Unused | Consume any one character |
| `IN_BITMAP`, `IN_RANGES`, `IN_PAGES` | Typed class index | Consume one character belonging to the referenced class |
| `CATEGORY` | Predicate identifier | Consume one character satisfying a category predicate |
| `AT` | Position-test identifier | Check an anchor or boundary without consuming input |
| `MARK` | Capture-slot index | Record the current subject position |
| `GROUPREF` | Group index | Match the substring captured by that group |

Consuming instructions require the necessary subject input; `ANY` and `ANY_ALL`
fail at end of subject. Category and position-test identifiers encode Unicode,
ASCII, and locale distinctions where applicable, independently of subject
storage width. Predicate and test identifiers are specified below.

Capture slots are paired: slots 0 and 1 record Python group 1's start and end,
slots 2 and 3 record group 2, and so on. `GROUPREF` uses zero-based group
indices internally. The entire-match span is tracked separately. Assertion,
atomic, and general possessive bodies finish with local `SUCCESS`; ordinary
general repeat bodies reach `REPEAT_END_GREEDY` or `REPEAT_END_LAZY` and make
another decision. Caller requirements such as full-match end position or
zero-width iteration progress can reject a candidate
and require further backtracking rather than accepting it prematurely.

### Matching Opcode Bit Fields

For matching-related opcode families, use these fields within the low opcode
byte of an instruction word:

```text
bits 7-4: operation family
bits 3-2: operand form
bits 1-0: representation
```

| Representation bits | Meaning |
|---|---|
| `00` | Literal |
| `01` | Bitmap |
| `10` | Ranges |
| `11` | Pages |

| Operand-form bits | Meaning |
|---|---|
| `00` | Direct immediate |
| `01` | Compact indirect pool/table reference |
| `10` | Wide indirect reference with following full-width operands |
| `11` | Reserved |

The ordinary matching family includes `LITERAL` (direct literal), `LITERAL_RUN`
(compact indirect literal), `LITERAL_RUN_WIDE` (wide indirect literal), and the
representation-specific class operations below. Other matching operation
families reuse the low-bit meanings for ASCII-ignorecase runs, locale
membership, and locale-ignorecase membership. Not every combination is valid:
no direct-inline class payload or locale Unicode-page operation is selected.
Invalid/reserved combinations must be rejected by validation.

This structure applies to matching families rather than forcing all
control-flow or capture operations into literal/bitmap/range/page meanings.
Assign their opcode ranges separately, preserving operand-form bit positions
where practical. Numeric assignments follow.

### Opcode Numbers

These are the numeric assignments for the agreed program-construction slice.
Future additions, including unresolved locale singleton forms, use reserved
values and require coordinated emission/decoding/verification updates. This
internal format has no public bytecode compatibility commitment.

Reserve `0x00` as invalid. Matching families preserve the family/form/
representation fields above:

| Instruction | Compact/immediate | Wide |
|---|---|---|
| `LITERAL` | `0x10` | None |
| `LITERAL_RUN` | `0x14` | `0x18` |
| `IN_BITMAP` | `0x15` | `0x19` |
| `IN_RANGES` | `0x16` | `0x1A` |
| `IN_PAGES` | `0x17` | `0x1B` |
| `LITERAL_RUN_ASCII_IGNORECASE` | `0x24` | `0x28` |
| `IN_LOCALE_BITMAP` | `0x35` | `0x39` |
| `IN_LOCALE_IGNORECASE_BITMAP` | `0x45` | `0x49` |
| `NOT_LITERAL` | `0x50` | None |

Other compact/wide families retain operand-form bits 3-2; bits 1-0 distinguish
operations rather than literal/bitmap/range/page representations:

| Instruction | Compact | Wide |
|---|---|---|
| `MARK` | `0x74` | `0x78` |
| `GROUPREF` | `0x84` | `0x88` |
| `GROUPREF_ASCII_IGNORECASE` | `0x85` | `0x89` |
| `GROUPREF_UNICODE_IGNORECASE` | `0x86` | `0x8A` |
| `GROUPREF_LOCALE_IGNORECASE` | `0x87` | `0x8B` |
| `REPEAT_ONE_GREEDY` | `0xC4` | `0xC8` |
| `REPEAT_ONE_LAZY` | `0xC5` | `0xC9` |
| `REPEAT_ONE_POSSESSIVE` | `0xC6` | `0xCA` |

Every wide opcode is its compact counterpart plus `0x04`. This selects the
form, not a universal instruction length; operand counts remain defined by
each instruction's format. Wide names have the `_WIDE` suffix.

Target-bearing operations have one fixed full-width encoding, with no
compact or `_WIDE` variants. Their opcode form bits are `10`; former compact
target values are reserved:

| Instruction | Opcode | Words |
|---|---|---|
| `GROUPREF_EXISTS` | `0x79` | 3 |
| `BRANCH` | `0x98` | 2 |
| `JUMP` | `0x99` | 2 |
| `ASSERT` | `0xA8` | 3 |
| `ASSERT_NOT` | `0xA9` | 3 |
| `ATOMIC_GROUP` | `0xAA` | 2 |
| `REPEAT` | `0xB8` | 4 |
| `REPEAT_POSSESSIVE` | `0xBA` | 4 |

Standalone operations have no compact/wide variants. Their low bits select
the operation without form or representation semantics:

| Instruction | Opcode |
|---|---|
| `SUCCESS` | `0x60` |
| `FAILURE` | `0x61` |
| `ANY` | `0x62` |
| `ANY_ALL` | `0x63` |
| `CATEGORY` | `0x64` |
| `AT` | `0x65` |
| `REPEAT_END_GREEDY` | `0x67` |
| `REPEAT_END_LAZY` | `0x68` |

Values `0x66` and `0xB9` are reserved. All unassigned combinations are
reserved, including operation families `0xD`-`0xF`.

### Indexed Wide Forms And Engine Field Widths

Separate subject positions from references into the regex or execution stack.
Subject positions, capture-mark values, and repeat progress positions use
`int64_t`, with -1 for an unmatched mark. Regex/program, pool, capture-slot,
group, and stack/context indices remain 32-bit. Absent stack/context links use
`UINT32_MAX`. Compiled repeat bounds retain their 32-bit representation.
Dynamic repeat counters are `uint64_t` and count actual completed iterations
without saturation. Normalize the compiled maximum to `uint64_t` on entry as
specified under general repeats below.

The agreed format ceilings are:

| Quantity | Maximum |
|---|---|
| Subject length | `INT64_MAX` elements |
| Pattern source length | `UINT32_MAX` code points/bytes |
| Instruction stream | `UINT32_MAX` words |
| Each literal pool or class table | `UINT32_MAX` elements/entries |
| Each context, repeat-record, or saved-mark vector | `UINT32_MAX` elements |
| Capturing groups | `INT32_MAX` groups |
| Capture-mark array | `0xFFFFFFFE` slots, two per group |

These are format ceilings, not allocation guarantees. Actual VM string/buffer
limits can be lower. Subject endpoints and captured positions are independent
of compiled-program size, so a small regex can match a subject larger than 4
GiB without widening its program or stack indices. Valid array/stack indices
are strictly below their counts; the maximum stack count therefore never makes
`UINT32_MAX` a valid entry index. Valid zero-based capture-group indices remain
representable in the signed last-group metadata.

Pool offsets count elements and program targets count words, not bytes. An
array's byte size can consequently exceed 4 GiB while its element indices
remain 32-bit. Check size multiplication, addition, growth, and conversion
before allocation or storing results. Do not use host-sized `size_t` fields in
engine records; native container/allocation sizes are converted explicitly. Do
not truncate or wrap subject positions, offsets, counts, or capture-slot
calculations such as twice the group count.

The following indexed instruction families have agreed two-word `_WIDE` forms:

```text
IN_BITMAP_WIDE
IN_RANGES_WIDE
IN_PAGES_WIDE
IN_LOCALE_BITMAP_WIDE
IN_LOCALE_IGNORECASE_BITMAP_WIDE
MARK_WIDE
GROUPREF_WIDE
GROUPREF_ASCII_IGNORECASE_WIDE
GROUPREF_UNICODE_IGNORECASE_WIDE
GROUPREF_LOCALE_IGNORECASE_WIDE
```

Each wide form has an opcode word with upper 24 bits zero, followed by a full
32-bit index. Use the existing one-word form when the index fits 24 bits, and
the wide form otherwise. Behavior is identical apart from instruction length.
`AT` and `CATEGORY` do not need wide forms for their small identifier sets.
`GROUPREF_EXISTS` and literal-run wide forms have two full operands and
therefore use three words, as specified in their own sections. Overflow/resource
errors follow Error Behavior.

### Position Tests

`AT` is one word: opcode in bits 0-7 and a test identifier in bits 8-31. The
compiler resolves effective scoped flags into that identifier. No ignorecase
variants or runtime flag stack are needed. Numeric test IDs are:

| Identifier | ID | Condition | Source syntax |
|---|---|---|---|
| `BEGIN_STRING` | `0x00` | Position is zero in the original subject | `\A`, or `^` without `MULTILINE` |
| `BEGIN_LINE` | `0x01` | Position is zero or the preceding character is `\n` | `^` with `MULTILINE` |
| `END_STRING` | `0x02` | Position equals effective subject end | `\z`, `\Z` |
| `END_OR_FINAL_NEWLINE` | `0x03` | At effective end, or immediately before its final `\n` | `$` without `MULTILINE` |
| `END_LINE` | `0x04` | At effective end, or the next character is `\n` | `$` with `MULTILINE` |

Here newline is specifically `\n`, not general Unicode line-separator
membership. `pos` starts the search but does not create a new subject
beginning; `endpos` establishes the effective end. At a valid search position,
the left boundary test can inspect the preceding character even if it is before
`pos`. The right test treats the effective end as absence, even if the original
string has more characters beyond `endpos`.

The remaining agreed test identifiers are:

```text
WORD_BOUNDARY_ASCII
NOT_WORD_BOUNDARY_ASCII
WORD_BOUNDARY_UNICODE
NOT_WORD_BOUNDARY_UNICODE
WORD_BOUNDARY_LOCALE
NOT_WORD_BOUNDARY_LOCALE
```

Compute word membership on each side, treating absent characters as non-word. A
boundary requires unequal membership; a non-boundary requires equal membership.
Consequently, on an empty subject `\b` fails and `\B` succeeds, matching Python
3.14. ASCII word membership is `[A-Za-z0-9_]`, Unicode membership is Python
Unicode alphanumeric membership plus underscore, and locale membership is
match-time locale alphanumeric membership plus underscore for bytes only. Share
these predicates with category and class matching.

Successful tests fall through without consuming input or changing stack/capture
state. Failed tests initiate ordinary backtracking. Word classification is
unaffected by `IGNORECASE`; `ASCII`, `LOCALE`, and `MULTILINE` distinctions are
already expressed by the selected test identifier. All tests must use checked
subject bounds.

Boundary-test IDs are paired so bit zero selects the complement:

| Identifier | ID |
|---|---|
| `WORD_BOUNDARY_ASCII` | `0x10` |
| `NOT_WORD_BOUNDARY_ASCII` | `0x11` |
| `WORD_BOUNDARY_UNICODE` | `0x12` |
| `NOT_WORD_BOUNDARY_UNICODE` | `0x13` |
| `WORD_BOUNDARY_LOCALE` | `0x14` |
| `NOT_WORD_BOUNDARY_LOCALE` | `0x15` |

Unassigned test IDs are reserved.

### Character Category Tests

`CATEGORY` is one word: opcode in bits 0-7 and a predicate identifier in bits
8-31. The agreed predicates are:

| Positive identifier | Negative identifier | Positive membership |
|---|---|---|
| `DIGIT_ASCII` | `NOT_DIGIT_ASCII` | `0`-`9` |
| `SPACE_ASCII` | `NOT_SPACE_ASCII` | Space, tab, newline, carriage return, form feed, vertical tab |
| `WORD_ASCII` | `NOT_WORD_ASCII` | `[A-Za-z0-9_]` |
| `DIGIT_UNICODE` | `NOT_DIGIT_UNICODE` | Unicode decimal characters |
| `SPACE_UNICODE` | `NOT_SPACE_UNICODE` | Python Unicode whitespace |
| `WORD_UNICODE` | `NOT_WORD_UNICODE` | Python Unicode alphanumeric membership plus underscore |
| `WORD_LOCALE` | `NOT_WORD_LOCALE` | Match-time locale alphanumeric byte membership plus underscore |

`\d`, `\s`, and `\w` select the positive digit, space, and word predicates;
`\D`, `\S`, and `\W` select their complements. The compiler selects the
identifier from effective scoped flags. Bytes default to ASCII, Unicode
patterns default to Unicode, `ASCII` selects ASCII predicates, and bytes
`LOCALE` changes only word/non-word membership. No ignorecase variants exist.
Assign identifiers 0-13 in table order, positive then negative for each row,
matching the class metadata selection bits below. All identifiers fit the
existing operand field without a wide form.

`CATEGORY` consumes exactly one character on success and does not modify
captures or push stack records. End of subject fails even for a negative
predicate: absence is not a character belonging to the complement. Failed tests
initiate ordinary backtracking. The operation is eligible for the one-character
repeat fast path.

Unicode decimal membership is narrower than Python `isdigit()`. Reference
probes show Arabic-Indic `٣` matches both `\d` and `\w`, while superscript `²`
matches `\w` but not `\d`. Unicode whitespace also differs from the ASCII set;
do not substitute platform locale predicates for fixed Unicode/ASCII behavior.

Share the underlying predicates with class records and `AT` boundaries, without
duplicating their definitions. Class membership can invoke the predicate
without consuming input; the main `CATEGORY` instruction performs consumption.
Keep this compact reference to shared predicates rather than require
per-pattern materialization of large Unicode category sets. No internal
linebreak category IDs are selected initially: Python `re` exposes no `\R`
escape, and dot/anchor newline semantics are already specified separately.

### Backreference Comparisons

Use four backreference opcodes, each encoded as one word with the opcode in
bits 0-7 and a zero-based group index in bits 8-31:

| Instruction | Comparison |
|---|---|
| `GROUPREF` | Exact code-point/byte equality |
| `GROUPREF_ASCII_IGNORECASE` | Equality after ASCII lowercase mapping on both characters |
| `GROUPREF_UNICODE_IGNORECASE` | Equality after Unicode simple-lowercase mapping on both characters |
| `GROUPREF_LOCALE_IGNORECASE` | Equality after match-time locale lowercase mapping on both byte values |

Select the mode from the scoped flags at the reference, not the flags that were
active while capturing the group. Read the group's current start/end marks;
fail if it did not participate or the remaining subject is too short. Compare
the captured substring with the current subject, advancing by its length on
success. An empty participating capture succeeds without consuming input. These
instructions do not write captures or push backtracking state.

Unicode backreferences must retain CPython's distinct simple-lowercase
comparison, not the expanded literal/class relation. For example, under
`IGNORECASE`, literal `i` matches `ı`, but `(i)\1` does not match `iı` in the
reference CPython. No full-string folding or multi-character expansion occurs.

Case-insensitive loops first test exact equality and only map unequal values.
ASCII lowercase conversion must be restricted to ASCII letters: captured text
can contain arbitrary characters, so an unconditional `| 0x20` is incorrect.
Use sufficiently wide temporaries for mapped code points regardless of subject
storage width. The locale form is bytes-only and uses the locale in effect at
matching time; the other forms are locale-independent. Overflow handling for
group indices remains part of the overall capture-limit policy.

### Group Participation Conditional

Use `GROUPREF_EXISTS` to select a branch based on whether a capture group
participated. It does not modify captures or push backtracking state.

| Instruction | Encoding |
|---|---|
| `GROUPREF_EXISTS` | Three words: opcode word with upper 24 bits zero; 32-bit zero-based group index; 32-bit absolute false target |

If the group participated, fall through after the entire encoding; otherwise
jump to the false target. An empty capture counts as participation and must
remain distinguishable from an unmatched group. Re-evaluate the current capture
state whenever the instruction is reached, including after outer backtracking.

Both operands are always full 32-bit words. Group indices remain subject to
the overall capture/mark limits. For `(a)?(?(1)b|c)`, the
conditional portion is:

```text
    GROUPREF_EXISTS 0, otherwise
    LITERAL 'b'
    JUMP done
otherwise:
    LITERAL 'c'
done:
    ...
```

Without an else branch, the false target points directly after the conditional.

### Alternatives And Jumps

Use `BRANCH` for ordered alternative chains and `JUMP` for ordinary control
transfer. `BRANCH` replaces the former `TRY` name and independent failure-record
semantics. Capture preservation follows the context model below.

| Instruction | Encoding |
|---|---|
| `BRANCH`, `JUMP` | Two words: opcode in bits 0-7 with upper 24 bits zero; 32-bit absolute target word index in the second word |

Targets count 32-bit words from the start of the instruction stream and must
identify an instruction's first word, never an operand word. Both forward and
backward transfers use the same full 32-bit unsigned absolute representation.
Internal program counters are `uint32_t`, matching context fields.
Forward references patch existing operand words without changing instruction
size or moving later instructions; no native pointers or signed relative
displacements occur in these operands.

- Each `BRANCH next_alternative` header describes an arm whose body starts
  immediately after the header. Its target identifies the next header or the
  chain's terminating `FAILURE` instruction.
- Entering the first header starts one alternatives handler. It walks subsequent
  headers itself, rather than creating a nested handler for each arm.
- `JUMP target` changes the current child's program counter. It neither
  completes the child nor commits an alternative.

For illustration, the matching control flow for `(ab|a)b`, with capture marks
omitted, is:

```text
    BRANCH alternative
    LITERAL_RUN "ab"
    JUMP continuation
alternative:
    BRANCH exhausted
    LITERAL 'a'
    JUMP continuation
exhausted:
    FAILURE
continuation:
    LITERAL 'b'
    SUCCESS
```

The handler saves subject position and capture metadata once. Under an active
repeat, also save the valid mark prefix. Execute the current arm and its shared
remainder as a child, inheriting acceptance checks, with the parent suspended
at `ResumePoint::AfterAlternative`. The parent's `saved_pc` identifies the
current header and lets it recover the next target.

Child success discards the handler's saved prefix, if any, and propagates
success outward with live captures retained. Child failure restores the saved
prefix without discarding it, where present, restores capture metadata and the
original subject position, and follows the next-header target. At the
terminating `FAILURE`, discard the save and return failure outward. Errors
release temporary saves and propagate unchanged. These are handler-specific
actions, not generic child-return rollback.

Against `ab`, the first arm consumes `ab`, jumps to the shared final `b`, and
fails there. Its parent can therefore try the second arm, which consumes `a`
and then the final `b`. Reaching the shared tail is not a successful return;
capture preservation must cover changes made in that tail as well as the arm.

Flat alternatives share one suspended handler; nested alternatives inside an
arm create their own. The structured builder/verifier must distinguish headers,
arm bodies, terminators, and shared tails. No ordinary transfer may enter the
middle of an existing chain as though it were its first header. Chain links
must advance through the finite emitted list; the full-width target format
does not make a cyclic chain valid. Construction scope rules are specified
under Label Scopes And Structural Targets. See the
pinned [branch handler](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L786).

### Execution Contexts And Capture Preservation

Use explicit suspended matching contexts and handler-selected capture saves,
following CPython 3.14.5. Ordinary consuming instructions do not push contexts.
A handler that needs a child computation saves its locals, selects an internal
resume point, and pushes a child. Child success/failure pops that context and
resumes its parent; the parent decides whether to restore state, try another
path, continue, or return outward. There is no automatic capture rollback on
every child failure. Errors propagate outward as errors, including through
negative assertions.

The agreed C++ context fields and internal resume states are:

```cpp
struct CaptureState
{
    uint32_t valid_mark_count;
    int32_t last_group; // -1 if none
};

enum class ResumePoint : uint8_t
{
    None,
    AfterAlternative,
    AfterRepeat,
    AfterMandatoryRepeatBody,
    AfterGreedyRepeatBody,
    AfterGreedyRepeatTail,
    AfterLazyRepeatTail,
    AfterLazyRepeatBody,
    AfterGreedyOneTail,
    AfterLazyOneTail,
    AfterPositiveAssertion,
    AfterNegativeAssertion,
    AfterAtomicGroup,
    AfterPossessiveMandatoryBody,
    AfterPossessiveOptionalBody,
};

struct RepeatLocals
{
    uint64_t count;
    uint64_t previous_count;
    int64_t previous_optional_pos;
    uint32_t repeat_index;
};

struct SimpleRepeatLocals
{
    uint64_t count;
    uint64_t maximum;
};

struct MatchContext
{
    int64_t saved_pos;
    uint32_t saved_pc;
    uint32_t saved_marks_base;

    CaptureState saved_captures;

    ResumePoint resume;
    bool check_acceptance;
    bool marks_saved;

    union
    {
        RepeatLocals repeat;
        SimpleRepeatLocals simple_repeat;
    };
};
```

The resume-state table defines how handlers use these fields. This is not a
packed ABI. Contexts form a LIFO vector, so the parent is the preceding element;
no parent index is needed. The successful endpoint travels in `ChildResult`, not
in the context. Use indices across growth and reacquire references after
pushes. Retain checked 32-bit scratch indices and the agreed 64-bit positions
and counts. `ResumePoint` values are executor states, not bytecode opcodes.

`saved_pc`/`saved_pos` restore the parent cursor. `saved_marks_base` is fixed
at this context's entry, not reassigned for each nested child. `marks_saved`
explicitly records whether this handler copied the valid prefix; an existing
nonempty saved-mark buffer can belong to a parent and is not evidence of a
local save. Initialize the flag to false and clear it on discard. An empty
prefix still permits a true flag, with zero payload. `None` means executing
rather than awaiting a child; resuming a parent with `None` is an internal
error. Set it back to `None` before resuming ordinary bytecode dispatch.

For ordinary repeat decisions, `repeat.count` is the completed count at the
current decision and `previous_count` is the invocation-record value to restore
on failed return through that decision. Save the old value explicitly rather
than reconstructing it by decrementing. `previous_optional_pos` preserves the
progress guard across optional body calls. `repeat_index` identifies the
invocation even while the tail temporarily selects its enclosing repeat.
`AfterRepeat` owns invocation cleanup before forwarding its child's result.

Simple repeats retain the candidate endpoint in `saved_pos`, plus count and
normalized maximum in their payload. Minimum, test, and tail can be recovered
from `saved_pc`; no persistent predicate cache is selected. Possessive repeats
use the repeat payload for invocation index and completed count. Other payload
fields are unused unless their continuation needs them; never read inactive
union members or locals not initialized by that handler.

`check_acceptance` determines whether successful completion applies whole-match
requirements, including fullmatch end position and iteration's rejected empty
match. Inherit it for computations matching the remainder of the pattern;
disable it for isolated assertion/atomic bodies. A child can finish before
the whole pattern does. The handler mapping must distinguish those cases from
propagating a successful complete remainder outward.

`MARK` writes the subject position directly to its live slot; a closing mark
also updates `last_group`. `valid_mark_count` represents the reference
highest-valid-mark index plus one. Start with zero valid slots and last group
-1. Slots beyond the valid prefix are logically absent regardless of their
stored contents. Extending the prefix initializes intervening slots as
unmatched (-1); result construction, backreferences, and participation tests
must respect validity rather than reading stale storage as a capture.

Saving `CaptureState` preserves validity and last-group metadata, not overwritten
positions. Where the reference handler requires mark preservation, copy the
active prefix `marks[0:valid_mark_count]` onto `saved_marks` and provide:

| Operation | Effect |
|---|---|
| Save marks | Append the current valid prefix |
| Restore and keep | Copy the saved prefix back, retaining the saved copy for another attempt |
| Restore and discard | Copy the saved prefix back and remove the saved copy |
| Discard | Remove the saved copy without changing live marks |

The saved metadata supplies the prefix length; restore that metadata at the
handler's reference restoration point. `saved_marks_base` is the saved-buffer
size on context entry. Child release truncates that child's temporary saves
to its entry base, leaving the parent's saves below it available to the
resumed handler. Check limits before appending. Whole-invocation error cleanup
releases scratch without turning an error into a failed match.

Preserve reference save conditions rather than introducing a universal
checkpoint policy: branches under an active repeat preserve the mark prefix;
greedy optional-repeat attempts also preserve it. Successful paths commonly
discard saves while retaining live captures; failed paths that continue may
restore and reuse a save. The exact decisions belong to the corresponding
handler mapping. In particular, `(a|(b))+` on `aba` must retain group 2's
earlier `b`; neither clearing captures at each iteration nor uniformly restoring
every child is correct.

Contexts and mark-prefix snapshots can still grow with matching work. This
replaces unconditional mutation history; it does not promise constant-space
captured repetition. Measure their costs in ES-08. See the pinned reference
[context/capture machinery](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L327).

### Matching Call And Return Protocol

Use one nonrecursive dispatch loop with live `uint32_t pc` and `int64_t pos`
locals. A suspended context saves both; its saved `pc` identifies the
suspending instruction so its resume handler can reread operands. The child's
successful endpoint is separate from that saved parent position:

```cpp
struct ChildResult
{
    bool matched;
    int64_t end_pos; // Meaningful only when matched.
};
```

This is executor state, not a Python result object. Before a child call, the
handler performs its required capture saves and repeat-state changes, saves
its cursor/locals and resume identifier, then pushes a child. The child records
the current saved-mark buffer size as its entry base. Set live `pc`/`pos` to
the child's entry/start and dispatch; lookbehind can have a different start
from the saved parent position. Inherit acceptance checks for remainder
computations and disable them for local assertion, atomic, and possessive
bodies. Allocation failure takes the invocation error path, never a failed
child continuation. Reacquire vector references after any growth.

On normal child return, its handlers first complete their required cleanup.
Record success and endpoint, or failure with no meaningful endpoint. Release
its temporary mark saves to its entry base, pop the child, restore the parent
cursor, and dispatch the parent's resume identifier with `ChildResult`. This
does not generically restore captures, repeat counts, or active-repeat
selection; the resumed handler owns those actions.

Always forward `result.end_pos` when propagating success, never the restored
parent position. `SUCCESS` returns success unless this context's acceptance
checks reject it; rejection is ordinary failure so pending continuations can
retry. A failed character test or `FAILURE` returns ordinary failure. Errors,
including interrupts and allocation failures, abort the invocation through one
scratch-cleanup path without entering ordinary failure continuations or being
inverted by assertions. At the root, success supplies the final endpoint and
live captures; failure reports no match. Reset scratch/capture-validity metadata
before another search attempt.

General repeat entry creates the invocation, then pushes an initial decision
context with count zero and previous count zero, suspending its owner at
`AfterRepeat`. Enter the common greedy/lazy decision code without executing
the ending's increment. An ending reached after a body preserves the record
count, computes a checked completed count plus one, and enters that same
decision code. These are internal dispatch transitions, not an unsigned -1
sentinel or an extra bytecode instruction. See the reference
[call machinery](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L477)
and [successful return](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L643).

### Resume-State Transitions

Generic return has already restored `saved_pc` and `saved_pos`. In the table,
forward success means return the child's endpoint with live captures retained.
Restore captures means restore saved validity/last-group metadata and, when
`marks_saved`, the copied prefix. Discard removes the prefix without restoring
live captures and clears `marks_saved`; consume means restore then discard.
Keeping a restored save leaves it available for another attempt. Errors abort
the invocation rather than entering these ordinary result transitions.

| Resume state | Child succeeds | Child fails |
|---|---|---|
| `AfterAlternative` | Discard save; forward success | Restore captures, keeping the prefix copy for the next arm. Follow the next header and call its body; when exhausted, discard save and fail outward. |
| `AfterRepeat` | Restore enclosing repeat selection, release invocation record, forward success | Same record cleanup, then fail outward |
| `AfterMandatoryRepeatBody` | Forward success | Restore record count to `previous_count`; fail outward without local capture restoration |
| `AfterGreedyRepeatBody` | Restore previous progress guard, discard capture save, forward success | Restore previous progress guard, captures, and `previous_count`; consume save. Select enclosing repeat and call tail at `AfterGreedyRepeatTail`. |
| `AfterGreedyRepeatTail` | Restore this repeat as active; forward success | Restore this repeat as active and its `previous_count`; fail outward without another capture restoration |
| `AfterLazyRepeatTail` | Restore this repeat as active; discard save; forward success | Restore this repeat as active; restore/consume capture save. At maximum or blocked progress, restore `previous_count` and fail; otherwise prepare another body at `AfterLazyRepeatBody`. |
| `AfterLazyRepeatBody` | Restore previous progress guard; forward success | Restore previous progress guard and `previous_count`; fail outward without another capture restoration |
| `AfterGreedyOneTail` | Discard save; forward success | Restore captures. Above minimum, decrement count and endpoint and retry, retaining the save; otherwise discard save and fail. |
| `AfterLazyOneTail` | Discard save; forward success | Restore captures. If another matching character is permitted, consume it, increment count, and retry retaining the save; otherwise discard save and fail. |
| `AfterPositiveAssertion` | Retain captures; continue at assertion continuation with original position | Fail outward |
| `AfterNegativeAssertion` | Discard save; fail outward | Restore/consume capture save; continue at assertion continuation with original position |
| `AfterAtomicGroup` | Adopt child endpoint and continue at atomic continuation | Fail outward |
| `AfterPossessiveMandatoryBody` | Adopt endpoint and increment count; call next mandatory body, begin optional attempts, or finish at maximum | Restore enclosing selection, release invocation record, fail outward |
| `AfterPossessiveOptionalBody` | Discard save, adopt endpoint, increment count; finish on empty progress/maximum, otherwise save state and call next optional body | Restore/consume capture save, keep pre-attempt position/count, finish repetition and enter tail |

Before an ordinary repeat body call, set the invocation count to this decision's
`repeat.count` and keep this repeat active. For an optional body, save the old
progress guard and set it to the current position. Apply the path-specific
capture-save rule. Before a tail call, select the enclosing repeat; both tail
returns restore this repeat's selection. Each `previous_count` is captured on
entering its decision and remains unchanged through its body-to-tail fallback;
never overwrite it from a record already mutated by a child.

Finishing a possessive repeat restores the enclosing selection, releases its
record, and dispatches the tail rather than claiming whole-pattern success.
All increments are checked; simple retries respect minimum, maximum, subject
bounds, and the next character test before changing their endpoint. A resumed
handler calls another child, returns outward, or sets `resume` to `None` before
ordinary dispatch. Returning to a parent with `None` is an internal error.

### Scratch Ownership And Release Order

One matching operation or iterator owns native scratch. The compiled program
owns none of it; independent calls using the same pattern get independent
scratch. Use four inline-capacity vectors with no allocating container inside
an individual record:

```cpp
struct MatchScratch
{
    absl::InlinedVector<MatchContext, kInlineContexts> contexts;
    absl::InlinedVector<int64_t, kInlineMarks> marks;
    absl::InlinedVector<int64_t, kInlineSavedMarks> saved_marks;
    absl::InlinedVector<RepeatRecord, kInlineRepeats> repeats;

    CaptureState captures;
    uint32_t active_repeat; // UINT32_MAX if absent
};
```

| Storage | Logical owner | Normal release |
|---|---|---|
| Context | Matching computation | When the computation returns |
| Live marks | Scratch owner | Reused between attempts |
| Saved mark prefix | Suspending handler | Explicit restore/discard; context return releases remaining local saves |
| Repeat record | Repeat-entry handler | `AfterRepeat` or possessive completion/failure |

Repeat records use a LIFO vector, without a free list. Lifetimes nest with
matching computations, even when active links differ from allocation order.
For example, allocated records `[O, I, J]` can have active chain `J -> O` while
I is suspended. Releasing J reads its `previous_repeat`, restores that active
selection, then pops J, leaving `[O, I]` with O active. Never infer the active
repeat from the last vector element. At normal owner release, all children
that could reference that record have returned and the record must be the
last allocated record; verify this invariant in debug/tests.

Normal child return proceeds in this order:

1. The finishing handler releases any repeat record it owns and performs its
   specified capture-save actions.
2. Preserve `ChildResult`, including the endpoint on success.
3. Truncate saved marks to the returning context's entry `saved_marks_base`.
4. Pop the context.
5. Reacquire its parent by index, restore the cursor, and run its continuation.

A parent saves its capture prefix before pushing a child. The child's entry
base lies above that prefix, so releasing the child's saves cannot remove the
parent's restoration data. Store indices and reacquire pointers/references/spans
after any operation that can grow the corresponding vector; none may survive
that vector's growth.

Errors and interrupts abort the attempt without ordinary failure continuations
or replaying capture history. Invalidate active-repeat selection, clear
contexts, records, and saved marks, and reset capture-validity metadata. These
are plain native values; no semantic unwind interpreter is required. Buffer
RAII releases allocations when the scratch owner is destroyed.

On normal root completion, contexts, repeat records, and saved prefixes are
empty. On success, live marks and capture metadata remain available until the
caller constructs or copies its result; only then may the next attempt reset
them. Between search attempts retain capacity, reset valid mark count to zero
and last group to -1, and keep mark storage allocated. Validity rules prevent
old contents from becoming captures. Require empty context/save/repeat vectors
before starting another attempt.

Check growth and byte-size arithmetic before allocation, applying checked
32-bit element-index ceilings separately to each vector and the existing
capture-slot limit to live marks. Allocation failure takes the engine-error
path. No per-record allocation, cancellation protocol, or rollback interpreter
is introduced.

### Interrupt Polling And Safepoint Integration

**Unresolved integration decision (AR-12):** defer the shared runtime polling
mechanism, polling cadence, signal delivery policy, and GC-request handling.
Interrupts and safepoints may eventually share a request mechanism, but checking
a request is distinct from entering a safepoint.

The agreed engine constraint is an easy, explicit error exit from matching.
When a non-safepoint check reports a keyboard interrupt for this operation,
abort through the native execution-error path, release scratch, and return a
typed interruption error. Do not report no match, retry another candidate, or
let a negative assertion invert the error. Do not create a managed exception
inside that path. The runtime binding constructs or propagates the appropriate
exception only after relinquishing borrowed views at a safe runtime boundary.

Under the current borrowed-subject-view contract, matching may hold raw,
unprotected storage pointers. Neither an interrupt check nor its native error
cleanup may enter a GC safepoint or otherwise perform GC-capable managed work.
The eventual polling interface must make that restriction explicit; no concrete
hook signature or new safepoint API is selected here.

A generic safepoint request is not itself a keyboard interrupt. Handling a
GC-only request during a long match remains open: deferral and yielding to a
safe boundary have different latency and resumption requirements. Do not
silently select either, or assume the matcher can service the request in place.
The shared request/signal mechanism and its thread/race behavior belong to the
later runtime design. Preserve this boundary while implementing ordinary error
propagation and cleanup.

### Context Verification

The context contracts are agreed. Implement the ES-07 representation slice and
verify the [worked traces](#execution-trace-examples) when matching lands.
Include forced vector growth, allocation/interrupt failure, copied results
surviving scratch reuse, and measured memory/context costs. AR-12 separately
tracks unresolved non-safepoint polling and GC-request integration. Inline
capacities and performance tuning require measurements.

### Assertion And Atomic Regions

Execute assertion and atomic bodies as local child computations ending with
`SUCCESS`. Child contexts run with
`check_acceptance = false`; the enclosing computation retains its acceptance
setting for subsequent matching.

| Instruction | Encoding |
|---|---|
| `ASSERT`, `ASSERT_NOT` | Three words: opcode with unused upper bits zero; full 32-bit absolute continuation target; 32-bit lookbehind distance |
| `ATOMIC_GROUP` | Two words: opcode with unused upper bits zero; full 32-bit absolute continuation target |
| `SUCCESS` | One word; complete this child, or the whole computation as appropriate |

The body begins after the entry; the continuation follows the body's local
`SUCCESS`. For `(?=(ab))ab`:

```text
    ASSERT continuation, back=0
    MARK 0
    LITERAL_RUN "ab"
    MARK 1
    SUCCESS
continuation:
    LITERAL_RUN "ab"
    SUCCESS
```

The existing matching context retains the entry instruction location, subject
position, and any required capture save. Use internal continuation states
`AfterPositiveAssertion`, `AfterNegativeAssertion`, and `AfterAtomicGroup`.
No separate runtime region payload is required.

| Handler | Body succeeds | Body fails |
|---|---|---|
| Positive assertion | Keep captures, restore original subject position, continue | Fail outward |
| Negative assertion | Fail outward | Restore saved capture state and position, continue |
| Atomic group | Keep captures and consumed position, continue | Fail outward |

Positive assertions and atomic groups do not take unconditional entry capture
snapshots. Their failed outward paths leave necessary restoration to the
corresponding enclosing handler. Negative assertions save capture metadata and,
under an active repeat, the valid mark prefix. On child failure, restore these
before continuing. On child success, discard any saved prefix and fail outward;
do not invent a universal local restoration step on that failed outward path.
Errors propagate unchanged and are never inverted by negative assertions.

Internal alternatives remain available while finding a successful body match.
Once the body returns success, its internal suspended computations have also
returned; later failure in the outer tail cannot reopen them. This gives both
atomic groups and successful positive assertions their reference commitment
behavior without scanning for retries or retaining disabled records. Captures
retained on success can still require restoration by an enclosing branch or
repeat if its own computation subsequently fails.

For lookbehind, start the child at original position minus the fixed distance;
retain the ordinary subject bounds. The compiler's fixed-width analysis ensures
the required consumption rather than imposing a new shortened subject end,
which could change nested assertion behavior. Insufficient preceding input
makes a positive assertion fail and a negative assertion continue without
running its body. Distance zero also serves ordinary lookahead.

Reference regressions include ordinary `(a|ab)c` succeeding on `abc` while
`(?>a|ab)c` fails; `(?=(a|ab))\1c` also fails on `abc`. On `ab`,
`(a)(?!(b)c)b` yields captures `("a", None)`. On `a`, both
`(?:(?=(a))ab|a)` and `(?:(?>(a))b|a)` yield an unmatched group 1 after the
outer alternative restores state. These six probes passed local CPython 3.14.5.
See the pinned reference [assertion handlers](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L1443)
and [atomic handler](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L1299).

### Character Classes

Include compressed Unicode bitmap pages in the initial engine, rather than
defer them to a later performance investigation. Retain CPython's hybrid
membership strategy: small classes can use literals/ranges, fragmented sets can
use bitmaps, and categories such as word, digit, and whitespace remain explicit
predicates. Testing a class does not introduce backtracking alternatives.

Store typed class records and their payloads separately from the instruction
stream and literal pool. A class describes membership, not an ordered literal
sequence. Representation is selected by the opcode, not a second `ClassKind`
dispatch in a generic descriptor:

| Instruction | Encoding |
|---|---|
| `IN_BITMAP`, `IN_RANGES`, `IN_PAGES` | One word: opcode in bits 0-7, 24-bit index into the corresponding typed class table in bits 8-31 |
| Corresponding `_WIDE` forms | Two words: opcode word with upper 24 bits zero; full 32-bit class index |

The agreed typed records are:

```cpp
struct BitmapClass
{
    uint32_t flags_and_categories;
    uint32_t bitmap_offset;
};

struct RangeClass
{
    uint32_t flags_and_categories;
    uint32_t ranges_offset;
    uint32_t range_count;
};

struct PagedClass
{
    uint32_t flags_and_categories;
    uint32_t page_start;
    uint32_t page_end;
    uint32_t directory_offset;
};
```

`flags_and_categories` stores outer negation and category-predicate selection:

| Bits | Meaning |
|---|---|
| 0 | Negate the entire class |
| 1, 2 | ASCII digit / non-digit |
| 3, 4 | ASCII space / non-space |
| 5, 6 | ASCII word / non-word |
| 7, 8 | Unicode digit / non-digit |
| 9, 10 | Unicode space / non-space |
| 11, 12 | Unicode word / non-word |
| 13, 14 | Locale word / non-word |
| 15-31 | Reserved; must be zero |

Number the 14 `CATEGORY` predicates consecutively in this order, so the class
selection bit is `uint32_t{1} << (1 + category_id)`. Explicit membership and
selected predicates form a union, followed by outer negation:

```cpp
bool member = explicit_membership(ch) || selected_category_matches(ch);
return member != outer_negated;
```

Short-circuit category evaluation when explicit membership succeeds. Positive
and negative predicates have separate bits: `[a\D]` is explicit `a` OR
non-digit, while `[^a\d]` negates explicit `a` OR digit. The metadata contains
no representation, case-insensitivity, or subject-width field. Opcodes select
representation and locale behavior; fixed case expansion is stored in the
membership data. Locale-ignorecase is an exception to the usual final-negation
rule: it combines complete class tests, including their negation, for each
mapped value, as specified under Locale-Sensitive Bytes Classes.

Class records contain data references, not a representation tag. A
category-only class can reference an empty bitmap and select its category
predicates. Payloads use shared storage: `ranges_offset` counts
`CodePointRange` elements, `directory_offset` counts `uint32_t` directory
entries, and bitmap offsets count `uint64_t` words.

Ranges are sorted, disjoint inclusive 32-bit endpoint pairs, with overlapping
or adjacent ranges merged during compilation. A bitmap is four 64-bit words
covering values 0-255; Unicode input above that range has false explicit bitmap
membership before categories/outer negation are considered. The same bitmap
representation serves bytes and low-valued Unicode classes. A paged class uses
a trimmed page directory and deduplicated 256-bit pages. Its page interval is
`[page_start, page_end)`, with exclusive `page_end`, so the range test is
`page_start <= page && page < page_end`. Within the interval, the directory
entry is `directory_offset + page - page_start`; the directory length is
`page_end - page_start`. Validate ordered bounds before subtracting and check
directory/bitmap slices against their storage bounds.

Bitmap payload storage uses `uint64_t` words for both standalone
byte/low-valued Unicode bitmaps and compressed Unicode pages. Each 256-bit page
remains 32 bytes. Each directory entry contains a 32-bit offset into the shared
`uint64_t` bitmap-word pool; `directory_offset` itself indexes the separate
`uint32_t` directory pool. Instruction words remain 32-bit. Explicit page
membership uses:

```cpp
uint32_t page = ch >> 8;
if (page < cls.page_start || page >= cls.page_end)
    return false; // No explicit page membership.

uint32_t offset = directory[cls.directory_offset + page - cls.page_start];
return (bitmap_words[offset + ((ch & 255) >> 6)]
        & (uint64_t{1} << (ch & 63))) != 0;
```

Descriptor validation must establish the directory bounds and four-word bitmap
slices before these accesses. Word and bit selection use `>> 6` and `& 63`, not
the `>> 5` and `& 31` operations of a 32-bit bitmap-word representation.

| Class contents | Representation |
|---|---|
| Single included/excluded character | Lower to `LITERAL` / `NOT_LITERAL` where matching semantics permit |
| Explicit bytes membership | 256-bit bitmap (32 bytes) |
| Small Unicode sets | Merged literal ranges |
| Larger fragmented Unicode sets | Compressed bitmap pages |
| Character categories | Predicate identifiers, combined with explicit membership |

Compressed pages divide code points into groups of 256. A page-index lookup
selects a deduplicated 256-bit bitmap, and the low eight code-point bits select
the bit within it. The same structure covers all Unicode code points, including
supplementary characters and lone surrogates, without a special BMP split.
There are 4,352 possible pages; validate `page_start`/`page_end` against that
domain and validate the directory slice and each four-word bitmap reference.

Trim the directory to its first and last occupied pages. Internal empty gaps
still occupy directory entries. A full-range directory consequently needs 4,352
32-bit entries (17,408 bytes); this directory cost must be considered when
choosing a representation for sparse sets.

Intern identical four-word bitmaps across the compiled program, including
all-zero, all-one, and identical partial pages. Use ordinary shared bitmaps for
empty/full pages initially, not specially tagged directory entries. Standalone
bitmaps can reference the same pool. Compiler deduplication tables need not
survive compilation; the resulting directory and bitmap pools are immutable.

### Class Representation Selection

Use a deterministic initial policy on normalized explicit membership after
fixed-rule case expansion and merging overlapping/adjacent ranges. Keep
category predicates and outer negation separate; select storage for the
explicit members before the outer complement is applied.

First apply existing singleton lowerings where semantics permit: a fixed
singleton without category terms can become `LITERAL` or `NOT_LITERAL`.
Locale-sensitive classes retain dynamic handling. Then select:

| Class | Representation |
|---|---|
| Bytes class | 256-bit bitmap |
| Unicode class with one or two merged ranges | Ranges |
| Remaining Unicode class entirely within 0-255 | 256-bit bitmap |
| Other Unicode class | Compare intrinsic range and page storage costs below |

For the last case, estimate bytes using the agreed record and payload layouts:

```text
range_cost = 12 + 8 * range_count
page_cost  = 16 + 4 * (page_end - page_start) + 32 * unique_bitmap_count
```

Count identical bitmaps once within the class, including empty/full pages when
needed. Choose pages when `page_cost <= range_cost`, otherwise ranges. Compute
costs in 64-bit arithmetic. Estimate intrinsic class cost before cross-class
sharing, then deduplicate selected bitmaps across the program; the choice must
not depend on which other classes happened to compile first. A category-only
class can use the shared empty bitmap plus its predicate mask.

Small range lists use a few direct comparisons. Larger lists first reject
outside their first/last endpoints, then binary search. Bitmaps reject explicit
membership above 255 before the bit test; pages reject outside their page
interval before directory/bitmap lookup. These are explicit-membership results,
not final results when predicates or outer negation are present.

The two-range threshold and larger-set size comparison are initial tuning
choices, not semantic requirements or established performance optima. Benchmark
small sets, fragmented BMP sets, sparse supplementary sets, and both successful
and failing inputs before changing thresholds or allowing pages a size premium
for lookup speed. The smallest encoding is not assumed to be universally
fastest.

### Class Membership And Fixed Case Expansion

For fixed ASCII/Unicode rules, membership is the union of explicit sets/ranges
and category predicates, followed by class negation. Ignorecase membership must
account for equivalent characters before that final negation: `[^a]` under
`IGNORECASE` rejects both `a` and `A`. Lowercasing range endpoints alone is not
a correct implementation of Unicode case-insensitive ranges.

For fixed ASCII/Unicode case rules, expand explicit literal and range members
at compile time into the required matching sets, then select ranges, bitmaps,
or compressed pages as usual. General fixed-rule case-insensitive classes use
the ordinary representation-specific class opcodes, with no runtime case
conversion or separate ignorecase class opcode. Preserve category predicates
with their effective ASCII/Unicode semantics and combine membership before
final negation. The expansion must match Python regex behavior rather than
full-string case folding; supplementary characters and equivalent code points
wider than the source literal must not be lost. Unicode data provenance is
specified under Shared Unicode Data; the initial representation-selection
policy is specified above.

An 8-bit Unicode subject still uses Unicode predicates and case rules. The same
compiled class records serve every Unicode subject width.

### Locale-Sensitive Bytes Classes

The agreed locale-sensitive class instructions are:

| Instruction | Encoding and behavior |
|---|---|
| `IN_LOCALE_BITMAP` | One word: opcode in bits 0-7, class index in bits 8-31; test membership using match-time locale predicates |
| `IN_LOCALE_IGNORECASE_BITMAP` | Same encoding; test membership using match-time locale case mappings and predicates |

These are bytes-only operations. The compiler fixes whether locale-sensitive
and case-insensitive matching is requested, but the actual locale rules are
those in effect at matching time. Unicode (`str`) patterns reject `LOCALE`,
including `IGNORECASE | LOCALE`. Ordinary bytes `IGNORECASE` without `LOCALE`,
Unicode `IGNORECASE`, and Unicode `ASCII | IGNORECASE` use fixed rules and
retain the compile-time lowering described above.

A locale class retains its original fixed members in a 256-bit bitmap,
locale-dependent category predicates, and an outer-negation flag. Do not bake
compilation-time locale-dependent membership or case expansion into that
bitmap. Bytes digit and whitespace categories remain ASCII-defined; locale
affects word/non-word membership and case matching. The bitmap class record is
defined above; locale word/non-word predicates are specified above.

For `IN_LOCALE_BITMAP`, combine the fixed bitmap membership and selected locale
predicates for the input byte, then apply outer negation. For
`IN_LOCALE_IGNORECASE_BITMAP`, match CPython's complete per-mapping class test:

```cpp
auto lo = locale_lower(ch);
auto up = locale_upper(ch);
return complete_class_test(lo)
    || (up != lo && complete_class_test(up));
```

Here `complete_class_test` includes explicit members, selected categories, and
outer negation. Do not move the complement outside the OR. This observable
CPython 3.14.5 behavior is surprising even in the C locale: `[^a!]` and
`[^a-z]` under bytes `IGNORECASE | LOCALE` match `a` and `b`, respectively.
Preserve the behavior rather than claiming parity for a different complement
rule. See the reference [class helper](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L166).

Locale-ignorecase literal and negated-literal operations must retain their
distinct exact-equality-first comparison, with negation applied to that entire
singleton relation. They cannot unconditionally lower to the general class
operation. Reference lowering can make source form observable: in the C locale,
`[^a]` and `[^aa]` reject `a`, while `[^a-a]` accepts it under these flags.
Keep enough AST provenance to reproduce the reference choice; normalizing
equivalent member sets must not silently merge these semantics.

On macOS with `en_US.UTF-8`, literal byte `0xff` matches itself while the class
`[\xffz]` does not, because locale conversion can yield `WEOF`. Do not narrow
mapping results into bytes before testing them. Preserve the reference
platform's mapping behavior and category checks, including out-of-byte mapped
values. See the reference [literal helper](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre.c#L210).

The exact encoding for distinct locale singleton tests and source-sensitive
lowering remains open (AR-03 below). Do not implement the superseded blanket
literal-to-class lowering while that decision is pending. Both general locale
class instructions are eligible one-character tests for simple repeats;
eligibility of the eventual singleton encoding must be specified with it.
Fixed members without locale-dependent predicates or ignorecase needs can still
use ordinary `IN_BITMAP`. `GROUPREF_LOCALE_IGNORECASE` supplies the separate
runtime backreference comparison.

Compiled class records remain immutable. Evaluate dynamic rules directly in the
initial design. A bitmap materialized using the matching-time locale is a
possible later optimization, not an unconditional property of the compiled
pattern; no locale-result cache or per-match materialization policy is
selected.

### General Repeat Instructions

Use one `REPEAT` entry and mode-specific `REPEAT_END_GREEDY` or
`REPEAT_END_LAZY` endings. A body may contain arbitrary
operations, including captures, alternatives, assertions, and nested repeats.

Entry always has four words:

```text
word 0: opcode in bits 0-7, upper 24 bits zero
word 1: uint32_t absolute repeat-ending word index
word 2: uint32_t minimum
word 3: uint32_t maximum
body follows
```

Both endings are one-word instructions with unused upper bits zero. The body
starts after the entry; the tail starts after the ending. There are no compact
or `_WIDE` control-target forms. For `(ab){2,5}`:

```text
    REPEAT decision, min=2, max=5
body:
    MARK 0
    LITERAL_RUN "ab"
    MARK 1
decision:
    REPEAT_END_GREEDY
tail:
    ... remainder ...
```

The invocation's repeat record has this initial shape:

```cpp
struct RepeatRecord
{
    uint64_t count;
    uint64_t maximum;
    int64_t last_optional_pos;
    uint32_t previous_repeat;
    uint32_t repeat_pc;
};
```

The entry matching context owns the record's lifetime, until the complete
repeat computation returns. Use stable indices in inline-capacity scratch
storage; nested invocations have distinct records. Body and tail attempts use
ordinary suspended matching contexts. Saved count/progress locals belong to
those contexts and are restored explicitly by their handlers, not logged on
every mutation. The context fields and resume-state table specify saved-local
placement; the [execution traces](#execution-trace-examples) guide later matcher
verification.

Keep an unsigned 64-bit completed count starting at zero. Entry runs the first
decision directly without executing a body or incrementing; reaching an ending
after a body increments the count and makes the next decision. This adapts the
reference's initial -1 convention without an unsigned sentinel. Preserve the
previous full-width count at each continuation that must restore it.

| Situation | Greedy decision | Lazy decision |
|---|---|---|
| Below minimum | Attempt another body | Attempt another body |
| Minimum satisfied and another iteration permitted | Try body; on failure try tail | Try tail; on failure try body |
| Maximum reached | Try tail | Try tail |

A body attempt includes its ending and subsequent matching; one successful
iteration is not a child-success boundary. Later failure can therefore revisit
alternatives within earlier iterations. Tail attempts temporarily select the
enclosing repeat as active, restoring this repeat's selection on return. There
is no separate exit opcode and no active-selection undo record.

Preserve the reference capture-save distinctions:

- Mandatory body attempts do not unconditionally copy the mark prefix.
- Greedy optional body attempts save capture metadata and the valid prefix;
  failure restores them before trying the tail.
- Lazy tail attempts save metadata and preserve the prefix when an enclosing
  repeat remains active; failure restores the appropriate state before another
  body attempt.

Permit empty iterations; prevent another optional body at the same position
while its enclosing optional attempt remains active. Each invocation initializes
`last_optional_pos` to -1. Before an optional body call, save the previous
guard and set it to the current position. Restore the previous guard when the
suspended call returns, on either success or failure. Do not restore it merely
on reaching the body's repeat ending: that call includes later decisions and
the tail and has not returned yet.

Mandatory iterations below the minimum bypass the guard and do not set it.
Greedy decisions check the guard before another optional body, otherwise try
the tail. Lazy decisions always try their tail first; only after it fails do
they check whether another body is permitted. The guard belongs to an
invocation, not a global subject position. Simple one-character repeat bodies
cannot be empty and need no such guard.

Worked cases are in [Empty Iteration Traces](#empty-iteration-traces-and-progress-guards).

Maximum `0xFFFFFFFF` denotes unbounded repetition. Validate minimum/maximum
ordering and finite-bound representability. Normalize the maximum once:

```cpp
uint64_t maximum = encoded_maximum == UINT32_MAX
                       ? UINT64_MAX
                       : static_cast<uint64_t>(encoded_maximum);
```

Store it in the repeat record, retaining the minimum in bytecode. Count actual
completed iterations without saturation; check increments and report counter
exhaustion under the existing resource-error policy rather than wrapping.

General possessive repetition uses the dedicated handler specified below,
rather than an atomic wrapper around this computation. Specialized
one-character repetition also remains separate. Verify the mapping against the
pinned reference
[general-repeat handlers](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L1031).

### Simple One-Character Repeat Instructions

Use `REPEAT_ONE_GREEDY`, `REPEAT_ONE_LAZY`, and `REPEAT_ONE_POSSESSIVE` to
repeat one side-effect-free character-test instruction. Eligible tests are
`LITERAL`, `NOT_LITERAL`, `ANY`, `ANY_ALL`, `IN_BITMAP`, `IN_RANGES`,
`IN_PAGES`, `IN_LOCALE_BITMAP`, `IN_LOCALE_IGNORECASE_BITMAP`, and `CATEGORY`,
including the two-word wide forms of these class tests. Bodies with captures,
assertions, alternatives, or multiple operations use general repetition. In
particular, `(a)+` must preserve its capture writes rather than simply repeat
the literal test.

Compact repeat headers have two words, followed by one complete character test:

```text
word 0: opcode in bits 0-7, minimum in bits 8-31
word 1: uint32_t maximum
word 2 onward: one complete character-test encoding (one or two words)
tail follows the entire test
```

Use the corresponding `_WIDE` opcode when the minimum exceeds 24 bits: an
opcode word with upper bits zero, a full 32-bit minimum, a full 32-bit maximum,
and the complete one- or two-word test. The repeat opcode's `_WIDE` suffix
extends the minimum only; the embedded test opcode independently determines the
test's width. Maximum `0xFFFFFFFF` means unbounded, with the same finite-bound
validation as general repeats. Normalize the maximum once on entry to a 64-bit
value as above, keeping any scan length or derived subject endpoint wide enough
for the subject. This is local handler state, not a requirement to allocate a
general repeat context. There is no exit target: decode the header and test to
identify the tail. The test encoding is an embedded operand of the repeat, not
entered through ordinary fallthrough or a control-flow target. The handler
selects the character test once, resolves its full operand, and runs its scan
without returning to main bytecode dispatch or decoding the test width for
every character.

| Repeat header | Embedded test | Total instruction extent |
|---|---|---|
| Compact (two words) | One word | Three words |
| Compact (two words) | Two words | Four words |
| Wide (three words) | One word | Four words |
| Wide (three words) | Two words | Five words |

The additional eligible encodings are `IN_BITMAP_WIDE`, `IN_RANGES_WIDE`,
`IN_PAGES_WIDE`, `IN_LOCALE_BITMAP_WIDE`, and
`IN_LOCALE_IGNORECASE_BITMAP_WIDE`, each supplying a full 32-bit class index. A
large class index does not force general repetition. Validation requires
exactly one eligible side-effect-free character test, sufficient operand words,
a valid class index, and no control-flow targets into any part of the embedded
test. Other wide instructions, such as `MARK_WIDE` or `GROUPREF_WIDE`, are not
made eligible merely because their encoding has the same length.

| Mode | Initial execution | Retry after tail failure |
|---|---|---|
| Greedy | Consume the longest permitted matching run; require the minimum | Shorten by one character and retry the tail |
| Lazy | Consume only the required minimum | Test/consume one additional character and retry the tail |
| Possessive | Consume the longest permitted matching run; require the minimum | No repeat retry |

Greedy and lazy handlers retain their candidate endpoint and count in the
suspended matching context. Before tail attempts, save capture metadata and,
when an enclosing repeat is active, the valid mark prefix. Execute the tail as
a child, inheriting acceptance checks. On child failure, restore the saved
capture state and candidate endpoint before adjusting it for the next attempt.
On success, discard the saved prefix, if present, and propagate success with
live captures retained. Exhausted retries fail outward; errors remain errors.

Greedy execution scans once to the longest permitted endpoint, verifies the
minimum, then tries successively shorter endpoints. Lazy execution consumes
only the minimum initially; after a failed tail attempt, test/consume one more
character if permitted before retrying. Never pre-scan the whole potential run
for lazy execution. All endpoint arithmetic respects the 64-bit subject bounds.

Simple possessive execution scans the longest permitted run, verifies the
minimum, and continues directly into the tail. It retains no repeat retry.
These side-effect-free bodies need no general repeat record, and the handler
leaves the enclosing active-repeat selection unchanged. Decode the embedded
test once for a scan; the continuation can recover its encoding from the
instruction when resuming. Exact cached locals are part of context-layout
consolidation, not a reason to change the embedded test format.

There are no tagged repeat retry records, disabled-retry tags, or atomic commit
scans. Compare with the pinned reference
[simple-repeat handlers](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L818).

### General Possessive Repeat Instructions

Use a dedicated `REPEAT_POSSESSIVE` handler for arbitrary bodies, retaining the
same finite-bound checks, normalized 64-bit maximum, and checked 64-bit counter
as other repeats. Entry has four words: opcode with unused upper bits zero,
full 32-bit absolute tail target, minimum, maximum. No short target form exists.
Its opcode is `0xBA`, with no compact or `_WIDE` variant.

```text
    REPEAT_POSSESSIVE tail, min=2, max=5
body:
    ... arbitrary body ...
    SUCCESS
tail:
    ... remainder ...
```

Install a repeat record during body matching so nested handlers use the
reference capture-preservation conditions for an active repeat. Execute each
iteration as a local child with acceptance checks disabled, ending at local
`SUCCESS`. Release its completed contexts before starting another iteration.
Internal alternatives can be explored to find that iteration's first success;
that successful iteration is then committed and cannot be reopened to satisfy
later iterations or the tail.

First run the mandatory iterations. Each success retains captures and advances
the count; failure fails the entire repeat, with restoration left to the
corresponding enclosing handler. Mandatory empty iterations remain permitted.
For each optional iteration, save capture metadata and the valid mark prefix.
On success, discard the save, retain captures/position, and increment the
count. On failure, restore metadata, marks, and the pre-attempt position, then
stop repeating. An error propagates instead of taking that stopping path.

Stop at the maximum or after an empty optional success. The first optional
attempt remains permitted even if a preceding mandatory iteration was empty;
use the reference optional-loop progress behavior, not a blanket rejection at
that position. Restore the enclosing repeat selection and release the
invocation record before continuing into the tail. Tail failure never gives
back an iteration. There is no ordinary greedy history retained across
successful iterations, though each body can itself require substantial scratch.

Preserve the verified CPython 3.14.5 distinction:
`fullmatch("(?:a|ab){2}+", "aba")` fails, whereas
`fullmatch("(?>(?:a|ab){2})", "aba")` succeeds. The possessive handler commits
successful mandatory iterations individually, while an atomic wrapper can
backtrack internally to satisfy the minimum. On `aba`, `(a(b)?)*+` retains
captures `("a", "b")`; a failed optional attempt must not erase captures from
earlier successful iterations. Compare the pinned reference
[possessive-repeat handler](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L1187).

## Search Metadata

Store a small `SearchInfo` record in the compiled program, with larger tables
in its owned vectors. Search metadata selects candidate starting positions for
`re.search`; the matcher executes the same program from each candidate.
`re.match` tries only at `pos`, while `re.fullmatch` also requires completion
at `endpos`. Preserve their method-specific handling of inverted bounds: with
`pos > endpos`, search has no candidates and fullmatch cannot complete, but
match still attempts the pattern at `pos` and can return an empty match. Do
not share an unconditional inverted-bounds or remaining-width rejection across
these entry points.

```cpp
enum class SearchFilter : uint8_t
{
    None,
    LiteralPrefix,
    FirstCharacter,
};

struct SearchInfo
{
    uint64_t minimum_width;
    SearchFilter filter;
    bool requires_subject_start;
    uint32_t prefix_offset;
    uint32_t prefix_length;
    uint32_t first_character_test[2];
};
```

Fields for the unused filter are ignored. Prefix offsets refer to the existing
typed literal pool. The first-character test uses the existing side-effect-free
character-test encoding, including two-word wide class operands; an unused
second word is zero. Compiler analysis derives this metadata; the builder
validates its representation and storage references before publication. The
verifier does not prove that a prefix/filter is semantically necessary.

`minimum_width` is a conservative lower bound on consumed subject elements.
Reject search candidates when fewer elements remain. A shared minimum-width
precheck is applicable when `pos <= endpos`; for match with inverted bounds,
preserve reference execution behavior instead of treating the negative
remaining length as an automatic failure or normalizing it to empty input.
Nested repeat bounds can produce widths larger than 32 bits, so compute with
checked, capped 64-bit arithmetic:
cap at `uint64_t{INT64_MAX} + 1`, meaning no supported subject can be long
enough. This cap applies only to derived width analysis, not dynamic repeat
counters. Assertions consume zero; where a useful bound cannot be proved,
retain a safe lower bound, including zero.

An exact prefix must be consumed at the match's starting position by every
successful path. Examples: `abc.*def` has prefix `abc`, `(abc)def` has prefix
`abcdef`, and `abc|abd` has common prefix `ab`. `a?bc` has no nonempty common
prefix. Find occurrences in order and invoke the matcher at each candidate. For
longer prefixes, build a KMP overlap table in an owned `std::vector<uint32_t>`;
single-character prefixes use a simple scan. Initially always enter the matcher
at its normal entry point, preserving capture writes and other operations
encountered while consuming the prefix.

Without a useful exact prefix, a first-character filter can reject characters
that cannot begin a match. Reuse literal, class, or other side-effect-free
one-character tests. Alternative starts may be combined into a class only when
the compiler proves the union is a necessary condition. Omit the filter when
that cannot be established. Locale tests use matching-time rules just as in
ordinary execution. Initially omit both consuming filters for patterns that can
match empty, so valid empty candidates are never skipped.

Set `requires_subject_start` only when every successful path requires the
actual subject beginning, as in `\Aabc`. Search then has at most one candidate;
nonzero `pos` does not establish a new beginning. Line-start scanning and
searching for required literals later in the pattern are deferred. Every
optimization must preserve the first selected match and fall back to ordinary
candidate iteration when its precondition cannot be proved.

## Pattern And Match Objects

`Pattern` is the compiled regex object returned by `re.compile()`, analogous to
a function owning its code object. It owns the immutable compiled regex program
and retains the original pattern. Global flags and group-name metadata are
owned by and accessed through the program. Its methods, including `search`, `match`, and `fullmatch`, execute that program
with private per-call matching state. Reusing a `Pattern` does not require a
cache.

`Match` is the returned match result, represented by retained references and
owned capture spans rather than a tuple of eagerly copied capture strings:

```text
Match
    pattern  -> Pattern
    subject  -> original string or bytes-like subject
    captures -> [(start, end), ...]
    pos, endpos -> int64_t independently clamped search bounds
    last_group -> int32_t last closed capture index, or -1
```

The result's span array includes group zero for the whole match, followed by
the numbered captures. This does not change the executor's separate handling of
whole-match boundaries and capture marks. Use the agreed signed 64-bit subject
offsets, with an absent span for an unmatched group. The `Match` owns its
result spans independently of the executor's mutable scratch state and retains
its `Pattern` and original subject for its lifetime.

Snapshot bounds and last-group metadata together with spans before executor
scratch is reset or reused. `last_group` is zero-based internally and supplies
Python's `lastindex`/`lastgroup`; derive neither from the highest participating
group nor from spans. For example, `(a(b))` on `ab` has `lastindex == 1`;
the corresponding named outer/inner groups have `lastgroup == "outer"`.
The metadata remains independent when a callback retains a result or an
iterator advances.

`group()` extracts the requested text from the subject using these spans;
`span()` returns offsets; `groups()` constructs a tuple of captured values.
Named access resolves through the retained pattern's group metadata. Match
results therefore need no copy of the compiled program or eagerly copied text
for every capture. Python-visible result behavior remains compatible with the
original subject category.

Managed references in these objects use the existing heap-member ownership
contracts. Concrete subject-storage access during execution follows the
existing runtime/buffer contracts and is deferred from this regex object-model
discussion; no new storage-stability mechanism is selected here.

### Result Construction

Match CPython's result shapes and unmatched-capture conventions. Unicode text
results are `str`; byte results are `bytes`, including for supported bytes-like
subjects. Construct collection results directly from cursor spans where
possible, without temporary public `Match` objects.

`findall` returns a list whose item shape depends on the total capture count:

| Capture count | Each item |
|---|---|
| Zero | Whole matched text |
| One | That capture's text |
| Two or more | Tuple of capture texts in group-number order |

Named captures count normally; noncapturing groups do not. Unmatched captures
become empty text, not `None`. For example, `findall('(a)|(b)', 'ab')` returns
`[('a', ''), ('', 'b')]`. Matching empty captures also produces empty text.

For every separator match, `split` appends the preceding subject text followed
by all captured groups in group-number order. Append the final subject
remainder even when empty, preserving leading and trailing empty pieces.
Unmatched separator captures become `None`; matched empty captures become
empty text. For example, `split('(,)|;', 'a,b;c')` returns
`['a', ',', 'b', None, 'c']`. `maxsplit` counts separator matches, not appended
capture values: zero is unlimited, positive values cap the number of splits,
and negative values perform no splitting. Both collection APIs use the shared
empty-match progression.

Public `Match` access preserves a distinction between unmatched captures and
participating empty captures:

| Operation | Unmatched capture |
|---|---|
| `group(n)` | `None` |
| `groups(default)` | Supplied default, normally `None` |
| `groupdict(default)` | Supplied default, normally `None` |
| `start(n)`, `end(n)` | `-1` |
| `span(n)` | `(-1, -1)` |

A participating empty capture returns empty text and equal nonnegative
endpoints. Invalid group numbers or names raise `IndexError`. Share capture
slicing with an explicit unmatched-value argument, so each caller selects
empty text, `None`, or its caller-supplied default. Offsets remain directly
available from spans. These rules were checked against local CPython 3.14.5.

## Iteration And Empty-Match Progression

Resume repeated search at the preceding match's end. After an empty result,
reject another empty result at that same starting position, but still allow
ordinary backtracking to find a nonempty result there. Once search moves to a
later candidate, empty results are allowed again. For example, `finditer` with
`|a` on `a` yields spans `(0, 0)`, `(0, 1)`, and `(1, 1)` in that order.
Unconditionally stepping past an empty result would lose the middle match.

Use a native search cursor beneath the Python iterator:

```cpp
struct SearchCursor
{
    int64_t pos;        // Original search bound for Match.pos.
    int64_t endpos;
    int64_t next_start;
    bool reject_empty_at_start = false;
    bool exhausted = false;
};
```

The iterator retains the pattern and subject and owns reusable matching scratch
storage. Each advance searches from `next_start`, applying the empty-result
restriction only to that initial candidate. On success, save independent result
capture spans, original search bounds, and last-group metadata, then update:

```cpp
next_start = match_end;
reject_empty_at_start = (match_start == match_end);
```

Ordinary no-match marks exhaustion; errors propagate separately. An empty match
at `endpos` may be returned once; the next advance rejects it and exhausts the
search without incrementing beyond the subject boundary. Reset capture, repeat,
and backtracking state between searches while retaining scratch capacity. Do
not resume the preceding result's suspended backtracking stack.

The restriction is an execution parameter checked at whole-pattern `SUCCESS`:

```cpp
if (reject_empty && subject_pos == match_start)
    backtrack();
```

Rejecting the result only after the matcher returns would discard alternatives
that might consume input at the same position. Internal assertions and other
zero-width operations are unaffected. Search acceleration must preserve this
initial-candidate restriction when choosing or skipping candidate positions.

`findall`, `split`, and substitution reuse the native cursor's progression and
consume spans directly where possible, without constructing public `Match`
objects for every internal result. API-specific output rules remain with each
operation. This follows CPython's repeated-search `must_advance` behavior with
a more explicit internal field name.

## Replacement Programs And Substitution

Compile parsed replacement templates into a separate program with a separate
small instruction set and interpreter. Native `sub`/`subn` also has the
reference-compatible retained-literal path described below. Replacement code
is not stored in the regex program, does not use matching opcodes, and is not called by regex bytecode. A
compiled `Pattern` may be reused with different replacement templates.

The replacement instruction set contains literal insertion and capture
insertion. For the parsed-template path, parse and validate against the
`Pattern`'s capture count and group-name metadata before searching, resolve names to numeric references,
then reuse the resulting replacement program for every match in that
substitution call. Initially keep this program local to the operation with no
replacement cache. `Match.expand()` uses the same template compiler and
interpreter against its retained pattern and capture spans.

Represent the instructions as parts with owned literal storage:

```cpp
struct ReplacementPart
{
    enum class Kind : uint32_t { Literal, Group };
    Kind kind;
    uint32_t index;  // Literal-pool offset or capture number.
    uint32_t length; // Literal length; unused for Group.
};
```

The replacement program owns an inline vector of parts and a literal pool,
templated on byte versus Unicode literal storage as for matching programs.
Merge adjacent literal text. Group references address result spans, including
group zero for the whole match. The exact inline capacities remain routine
implementation choices.

For an escape-free literal replacement recognized by the reference's
classification rules, native substitution retains the original replacement
object without copying its contents into a program. Append that same object
to the retained output fragments for each selected match, then join at the
end. The owning reference belongs to the native substitution operation, not
to the standalone C++ regex program. Preserve category errors at the point
the reference uses the replacement rather than eagerly coercing it.

This timing is observable with mutable bytes-like replacements. A subject
exporter's `__buffer__` can change a replacement `bytearray` from `b"X"` to
`b"Y"` during subject adaptation; `sub(b"a", replacement, subject)` then yields
`b"YY"` for an exported `b"aa"`. A replacement containing an escape, such as
`bytearray(b"X\\n")`, follows the parsed-template path and snapshots its
literal text before subject adaptation, yielding `b"X\nX\n"` under the same
mutation. Preserve this difference when buffer exporters become supported.
`Match.expand()` continues to use the reference template-compilation path;
do not automatically apply the substitution-only literal shortcut to it.

For example, `\g<2>, \g<1>` compiles to `Group(2)`, `Literal(", ")`, and
`Group(1)`. Support numbered references (`\1`, `\g<1>`), named references
(`\g<name>`), and the whole match (`\g<0>`), with Python-compatible numeric
reference/octal disambiguation. Decode replacement escapes such as `\n` and
`\\` during compilation; replacement syntax has no regex matching operations,
quantifiers, or scoped flags. Retain source positions for diagnostics.

Validate syntax and references even when no matches will be found. Preserve
CPython's diagnostic distinctions: an invalid numeric group reference raises
`PatternError`, while an unknown group name raises `IndexError`. Unmatched
capture references expand to empty text. Expansion walks the instructions and
appends literal slices or captured subject slices, with checked output-size
arithmetic and no further template interpretation or case conversion.

The native `sub`/`subn` operation coordinates matching and replacement:

1. Classify a retained literal or compile/validate a parsed template against
   the pattern; callable replacements follow their own path.
2. Create a search cursor and output builder.
3. For each match, append preceding unmatched subject text, then retain the
   literal object or interpret the replacement program using capture spans.
4. Append the remaining subject text and return the result, with the count for
   `subn`.

Process one match at a time; do not first collect the complete set of matches.
Search always examines the original subject, so inserted output is never
searched again. Search progression and the end of already emitted subject text
are separate positions, particularly around empty matches. Count limits and
API-specific argument/error ordering must follow CPython.

### Replacement Callbacks

Match CPython's callback behavior. For each selected match in order, construct
an independent `Match` and invoke the callable with that single argument through
the ordinary runtime call API. Do not compile or interpret its returned text as
a replacement template. No matches means no calls. Count limits and the shared
empty-match progression determine which matches receive callbacks. `count=0`
is unlimited, a positive count caps replacements, and a negative count invokes
no callbacks and performs no replacements.

| Return value | Behavior |
|---|---|
| `str` for Unicode matching | Insert the text |
| Join-compatible bytes-like object for bytes matching | Insert the bytes |
| `None` | Insert nothing; still count the replacement |
| Other value | Raise `TypeError` during final output joining |

The callback path accumulates retained output fragments, then joins them at
the end. For each match, first save preceding unmatched subject text as a
fragment, then call the callback and retain its result object unless it is
`None`. Append the trailing subject fragment after iteration. Validate and join
the pieces using the appropriate Unicode or bytes join semantics. This refines
the general output-builder sketch: callback results are retained as objects,
not eagerly copied or type-checked at the callback boundary. Matching still
proceeds one result at a time; it does not collect all matches first.

Local CPython 3.14.5 probes show that returning `42` for three matches invokes
all three callbacks before the final `TypeError`. A later callback exception
therefore takes precedence over an earlier invalid return. Returning the same
mutable `bytearray` for two matches, changing it from `b"1"` to `b"2"` on the
second call, produces `b"22"`; immediate copying would change that behavior.
Callback exceptions stop substitution immediately and propagate unchanged.

Callbacks may retain their `Match` objects or invoke regex operations
recursively. Saved result spans must be independent of the outer cursor's
reusable scratch state. The substitution operation coordinates the calls;
the matching interpreter itself never invokes replacement callbacks.

## Argument Conversion And Error Ordering

Match CPython 3.14.5's conversion and ordering behavior. Validation is staged
across the Python wrapper, native argument converters, replacement preparation,
subject adaptation, execution, and result assembly. Do not introduce one
up-front validation pass that changes which exception wins.

| Argument | Conversion and interpretation |
|---|---|
| `pos`, `endpos` | Integer index protocol (`__index__`), checked signed 64-bit conversion; floats rejected and overflow reported |
| `count`, `maxsplit` | Same numeric conversion; zero unlimited, positive a cap, negative no replacements/splits |
| Flags | Python integer/`RegexFlag` bitmask behavior, not generic index conversion |

Process native position converters in signature order: `pos` before `endpos`,
both before subject validation. Propagate numeric-conversion exceptions before
examining later arguments. After validating the subject, independently clamp
positions into `[0, subject_length]`; negative positions mean zero, not indexing
from the end. Preserve method-specific behavior when the resulting
`pos > endpos`: `search`, `finditer`, and `findall` have no candidate positions,
and `fullmatch` cannot finish at the earlier end, but `match` still attempts
the pattern at `pos`. An empty pattern or some zero-width assertions can
succeed there, retaining the independently clamped `pos` and `endpos` on the
result. Do not reject inverted bounds globally or replace them with an ordinary
empty range; opcode-specific behavior still matters. Overflow is checked
before clamping, so an unrepresentably large integer is not silently clipped.
Keep counts signed until their zero/positive/negative meaning has been applied.

Module-level calls bind their Python arguments, resolve/compile the pattern
and flags, then invoke a native `Pattern` method. Only then do method numeric
conversions and operation-specific validation run. Preserve signature binding,
duplicate-argument handling, and reference warnings where applicable. Existing
compiled patterns are accepted unchanged when no effective flags are supplied;
nonzero flags with a compiled pattern raise `ValueError`. Preserve the
reference flag normalization and bitmask behavior, including acceptance of
unknown bits where CPython accepts them. Do not route flags through the
position/count converter. Pattern caching remains deferred, so its incidental
hash lookups are not part of this implementation.

The native substitution ordering is:

1. Convert `count`.
2. Identify a callable or prepare/compile the replacement template.
3. Validate the subject and pattern/subject bytes-versus-Unicode category.
4. Search and perform replacements.
5. Assemble the output with the appropriate joining rules.

Replacement preparation distinguishes a retained escape-free literal from a
parsed template, as specified above. Preparing an unused literal must not
snapshot a mutable replacement before subject adaptation.

An invalid template group reference can therefore win over an invalid subject.
A negative count still prepares the replacement and validates the subject.
Template syntax and references are validated before searching, but replacement
text category compatibility may fail only when that text is actually used.
For example, CPython returns `"zzz"` for `sub("a", b"X", "zzz")`, but raises
`TypeError` for `sub("a", b"X", "a")`. Do not reject an otherwise unused
replacement solely for its byte/Unicode category. The template's own literal
category must remain available so expansion/joining preserves these checks;
do not silently coerce it to the pattern's category. Callback returns follow
the deferred validation rules already specified above.

The Python wrapper owns public binding and flags; the native binding performs
sequential checked numeric conversions using the runtime index protocol, then
subject adaptation and operation-specific preparation in the reference order.
Group access has a separate error mapping: invalid `Match.group()` selectors
raise `IndexError`, not the `TypeError` used for invalid position/count types.
Use differential tests with two invalid arguments and observable `__index__`
methods to establish precedence and avoid reordering user-visible conversions.

## Error Behavior

Preserve CPython's existing exception distinctions and diagnostic structure.
Additional format/resource limits introduced by this engine have the following
explicit mappings:

| Situation | Outcome |
|---|---|
| Valid pattern does not match | Normal no-match result |
| Invalid regex syntax or semantics | `re.PatternError` |
| Invalid argument type or bytes/Unicode mismatch | `TypeError` |
| Invalid flag combination | `ValueError` |
| Numeric repeat bound exceeds supported range | `OverflowError` |
| Compiled program/pool exceeds the engine format limit | `OverflowError` |
| Subject length or supplied position cannot be represented | `OverflowError` |
| Dynamic repeat counter exhausts its representation | `OverflowError` |
| Allocation failure or execution-stack capacity exhaustion | `MemoryError` |
| Detected invalid internally generated bytecode/state | `RuntimeError` |

`re.PatternError` derives from `Exception`, and `re.error` aliases the same
class. Preserve `msg`, `pattern`, `pos`, `lineno`, and `colno`, including
reference cases where pattern or position is `None`. Work toward reference
diagnostic wording; new engine-limit messages should identify the exhausted
quantity.

AST source spans are internal information, distinct from the public diagnostic
position. `PatternError.pos` is a zero-based code-point position for Unicode
patterns or byte position for bytes patterns. `lineno` and `colno` are
one-based; there is no public end-position field. Error construction preserves
the unformatted `msg`, original pattern where supplied by CPython, and optional
position, deriving the formatted location suffix and line/column attributes.
Some errors, notably variable-width lookbehind, have no public pattern or
position in the reference behavior even when an internal span is available. For
`(a)\2`, the invalid-reference diagnostic points at the digit at offset 4, not
the escape's starting backslash. Parsing stops at the first error; retain
reference diagnostic precedence through later analysis. Typed diagnostics flow
to the binding's exception construction, with the exception families already
specified under Error Behavior.

Reference CPython 3.14.5 probes distinguish malformed syntax and reversed
repeat bounds (`PatternError`) from an oversized numeric repetition
(`OverflowError`). Variable-width lookbehind is `PatternError`, incompatible
flags are `ValueError`, and bytes/Unicode mixing is `TypeError`. Do not
collapse all compilation errors into one exception family or treat every
numeric-looking error as overflow.

Execution-stack capacity exhaustion is a resource error, even when the engine
hits its index capacity before an allocator fails. It is not recursive-call
exhaustion and must not become `RecursionError`. A genuine parser/compiler
nesting-depth guard, if required by the eventual implementation, is a separate
case where `RecursionError` is appropriate; no depth threshold is selected
here.

The engine distinguishes match, no match, and error explicitly. Its binding
translates typed errors into pending CloverVM exceptions using existing
explicit fallibility contracts. Negative assertions invert only ordinary match
failure. Search does not swallow errors and retry at another position. Existing
interrupt and replacement-callback exceptions propagate unchanged. Cleanup must
release per-call scratch storage and retained objects on all exits.

Counter overflow is never wrapped or saturated. Normalizing an unbounded
maximum to `UINT64_MAX` must not silently convert representational exhaustion
into an ordinary finite-repeat exit. No automatic timeout or backtracking-work
budget is introduced in this initial design; those would require separate
agreement.

## Slice 1: Program Construction And Inspection

Implement the program format, owning storage, builder, and inspection tooling
in `src/regex/`, namespace `cl::regex`. This slice emits and inspects programs;
it contains no matcher, parser/AST, Unicode generation, replacement interpreter,
Python binding, or changes to existing string/tokenizer behavior. Later users
consume the interfaces and encodings specified above. The slice 1 design
boundary is agreed: owning program/builder, typed pools and metadata, scoped
helpers/fixups, explicit root completion, and inspection/verification. Execution
contexts and runtime integration belong to later implementation slices.

Use these file boundaries:

| File under `src/regex/` | Responsibility |
|---|---|
| `error.h` | `ProgramErrorKind`, `ProgramError`, and `BuildResult` |
| `opcode.def`, `opcode.h` | Numeric opcodes and shared encoding/operand metadata |
| `operands.h` | Typed class references, literal slices, ranges/bitmaps, class metadata, `CharacterTest`, repeat bounds/modes, assertion polarity |
| `program.h` | Owning `CompiledProgram`, class storage records, program/search metadata, group-name records, const accessors |
| `program_builder.h/.cpp` | `ProgramBuilder`, target-local RAII `JumpTarget`, alternation/repeat/region helpers, pool interning, direct emission and freezing |
| `instruction.h` | Inspection-only `DecodedInstruction` and `DecodedCharacterTest` |
| `decode.h/.cpp` | Stateless bounds-checked instruction decoding |
| `verify.h/.cpp` | Debug/test whole-program verification |
| `disassemble.h/.cpp` | Readable instruction/pool/metadata inspection |

`CharacterTest` belongs in `operands.h`, shared by emission and later execution,
not among the inspection-only decoded types. Keep helpers that operate on
builder state with the builder, following `CodeObjectBuilder`'s organization.
Use explicit `uint8_t` and `uint32_t` instantiations for out-of-line templated
implementation; keep small const accessors inline. Engine interfaces depend on
native C++ values and `Result`, not `Value`, `ThreadState`, pending exceptions,
or interpreter frames. Add sources to the existing library build rather than
introducing a separate build project or public C API.

Tests join the existing GTest target without requiring a VM fixture:

| Test file | Scope |
|---|---|
| `tests/test_regex_program_builder.cpp` | Pool/metadata ownership, stable references, direct encodings, relocations, structured helpers, freeze/error behavior; parameterized over both literal types |
| `tests/test_regex_decode.cpp` | Operand normalization, instruction extents, compact/wide forms, malformed/truncated encodings, embedded repeat tests |
| `tests/test_regex_verify.cpp` | Invalid targets/pools/indices/metadata, reserved bits, improper region/repeat nesting, incomplete programs |
| `tests/test_regex_disassemble.cpp` | Readable representative output with word offsets, full target values, resolved pool content, and embedded tests |

The verification baseline is:

- Representative golden words for every encoding layout, plus decoding
  coverage of all selected opcodes. Do not duplicate the opcode definition
  table solely to test that its values agree with themselves.
- Forward, backward, and multiple references to a target, including references
  emitted after resolution. Patching never changes instruction sizes or later
  offsets. Reject unresolved references at freeze even if the helper has
  already been destroyed; verify ordinary early-return cleanup without
  cancellation or destructor assertions.
- Nested alternation/repeat/assertion/atomic helpers, correct ending and
  continuation positions, closing order, and unclosed-helper rejection. Include
  all four simple-repeat header/test-width combinations and reject targets anywhere
  inside the embedded test.
- Valid branch chains and local possessive bodies; mutated malformed chains,
  wrong endings, inconsistent tails, and prohibited cross-scope transfers.
  Distinguish root and local `SUCCESS` without simulating runtime contexts.
- Explicit root completion: a one-word empty-regex program succeeds; an empty
  stream, missing root success, local-only success, trailing emission/binding,
  wrong completion ownership, and fallthrough outside a scope/program fail.
  Verify that `freeze()` never appends success and `REPEAT_POSSESSIVE` encodes
  `0xBA` with its full tail target and bounds.
- Literal/capture/class compact-wide boundaries where applicable; control-flow
  targets always have the sole 32-bit form. Test ceiling arithmetic with
  synthetic counts rather than allocating multi-gigabyte pools.
- Copied caller buffers, surviving program ownership after builder destruction,
  immutable public access, stable pool indices, shared bitmap/page payloads,
  metadata defaults, group-name lookup ordering, and search metadata slices.
  Verify kind/location for malformed-program diagnostics and that allocation
  failure reporting itself requires no formatted/allocated message.

Construct valid programs through typed builders and helpers. Decoder/verifier
negative tests may operate on manually created word streams or test-local
copies mutated into malformed input; keep production storage private and avoid
adding mutable public access just for tests. Unicode identifier/name semantics
and the correctness of a prefix's necessity belong to later parser/Unicode
analysis tests, not this representation slice.

Run focused tests while developing and the required debug `all check` before
finalizing implementation. Run release `all check` at the exact commit before
push. Match behavior, CPython regex differential tests, and matcher performance
measurements begin in the slices that implement those capabilities.

## Open Decisions And Verification

Agreed implementation boundary and remaining design decisions:

- Execution-model and slice 1 construction decisions are agreed; their
  implementation and differential/resource verification remain ES-01 through
  ES-08 work. Locale singleton operations and source-sensitive lowering remain
  open (AR-03) for later parser/compiler/executor slices. Slice 1 implements
  the currently agreed opcode set without claiming those additions are final.
- Concrete binding APIs, native-resource cleanup, and the initial syntax/API
  slice, including buffer-protocol dependencies. Execution-time subject-storage
  mechanics remain deferred to runtime integration.
- Parser/compiler lowering details for later syntax/API slices. Slice 1 does
  not parse patterns or select semantic lowering.

### Adversarial Review Follow-up

Track the review of design commit `a657961b` here. "Fixed in design" means the
contract/sketch is corrected, not that an implementation or its regressions
exist. Do not drop open items when splitting the implementation into slices.

| ID | Item | State / next action |
|---|---|---|
| AR-01 | Published-program mutation through implicit special members | Fixed in design: delete copy/move construction and assignment; transfer only owning pointers. Add compile-time API tests in slice 1. |
| AR-02 | Moving a builder invalidates target/helper pointers | Fixed in design: builder and helpers are noncopyable/nonmovable; builder outlives helpers. Add slice 1 lifetime tests. |
| AR-03 | Locale-ignorecase classes differ from literal/negated-literal paths, including source-sensitive lowering and platform mapping results | Verified CPython behavior is recorded. **Open:** choose singleton encoding and preserve reference lowering; update opcode/test eligibility and class-normalization rules before implementing locale operations. |
| AR-04 | Eager replacement snapshots differ from the retained-literal path | Fixed in design: substitution retains escape-free literal objects and snapshots parsed templates at reference preparation time. Add mutable-replacement/exporter regressions when that protocol lands. |
| AR-05 | Match results omitted last-group and bounds snapshots | Fixed in design: retain `last_group`, `pos`, and `endpos` independently of executor scratch. Add nested-capture, iterator, and retained-callback regressions. |
| AR-06 | Unsigned flag storage could expose wrong Python values/ranges | Fixed in design: store bits, expose signed 32-bit values, and preserve reference signed-C-int conversion stage. Add negative/oversized flags and precedence tests. |
| AR-07 | Labels can bypass structured entries/endings despite close-order checks | Fixed in design: construction scope checks and independent three-pass verifier reconstruction/transfer checks agreed. Test entering and escaping structured bodies, chain roles, and completion ownership in slice 1. |
| AR-08 | Re-entering the normal matcher rechecks long overlapping prefixes | **Open:** consider a proved-safe prefix-skip continuation. Benchmark `"a" * M + "(?=b)"` on `"a" * N`; retain capture/whole-match correctness. |
| AR-09 | Nested successful regions repeatedly scan retained history | Old commit-scan design superseded by ES-05. **Verification still open (ES-08):** establish region completion/context release behavior and measure nested-region costs in the replacement. No active-choice-chain design is selected. |
| AR-10 | Captured repetition retains excessive undo history | Unconditional undo logging superseded by ES-01/02/04. The old design predicted 120N/200N bytes for exact/greedy captured repeats; those are not replacement estimates. **Verification still open (ES-08):** measure contexts and mark snapshots. Coalesced undo is not selected. |
| AR-11 | Capture reset per search candidate is unspecified | Fixed in design: reset valid-mark count and last-group metadata while reusing allocated mark storage; initialize gaps when extending validity. Verify that candidate reset does not clear every capture slot or expose stale captures in matcher tests. |
| AR-12 | Interrupt polling and safepoint integration | **Deliberately deferred:** engine supports easy typed interruption exits, but checks/cleanup must not enter GC safepoints or allocate managed exceptions while borrowed pointers are live. Polling cadence, shared request mechanism, signal delivery, and GC-only request handling remain unresolved; see Interrupt Polling And Safepoint Integration. |
| AR-13 | Ignorecase compilation can rescan all equivalence members for every source range | **Open:** assess one pass over grouped members against the normalized range set; benchmark many-range classes. |

The existing parser/compiler depth-guard question and buffer/runtime integration
remain open independently of this review. Performance follow-up must include
overlapping prefixes, deeply nested successful atomic/assertion regions, exact
and greedy captured repeats, and many-range ignorecase compilation; broad
benchmark categories alone will not necessarily expose these costs.

Compatibility regressions must include:

- C-locale bytes ignorecase `[^a!]`, `[^a-z]`, `[^a]`, and `[^a-a]` to preserve
  general-class versus singleton/provenance distinctions; also test invalid
  multibyte bytes such as `0xff` under an available UTF-8 locale on macOS.
- Escape-free mutable replacement references versus parsed escaped templates
  when subject adaptation mutates the original replacement.
- `(a(b))` on `ab` yielding `lastindex == 1`, and named outer/inner captures
  retaining `lastgroup == "outer"` after another match or recursive callback.
- Negative flags (`-1024`) for Unicode and bytes, the signed limit (`1 << 31`),
  and errors in earlier parsing/conversion stages.

Verification must establish nested context restoration/release ordering,
simple-repeat tail retries with compact/wide class operands, and search
analysis/scanning.
Measure class-selection thresholds and inline capacities before tuning them. No
timeout or backtracking-work budget is selected; caching remains deferred.

Use focused C++ tests for program structure and execution invariants, then
Python/interpreter tests and differential probes for observable semantics.
Adapt relevant CPython tests with attribution. Provide typed opcode
definitions, structural validation, and a readable disassembler. Preserve these
execution cases:

- `(a|(b))+` on `aba` preserves group 2's earlier capture of `b`.
- Successful positive assertions expose captures without consuming input.
- Atomic groups discard internal alternatives while retaining captures that an
  outer failure must still undo.
- Empty repetitions terminate while preserving capture behavior.

Bounds regressions must distinguish the entry points, including empty patterns
and assertions. The distinction follows the reference [match entry point](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre.c)
and [search's explicit inverted-bounds guard](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L1580).

Local CPython 3.14.5 probes on subject `"abc"`, `pos=2`, `endpos=1` give:

| Pattern | `match` span | `search` / `fullmatch` | `finditer` spans / `findall` |
|---|---|---|---|
| Empty pattern | `(2, 2)` | `None` | `[]` |
| `(?=)` | `(2, 2)` | `None` | `[]` |
| `(?!x)` | `(2, 2)` | `None` | `[]` |
| `(?=c)` | `None` | `None` | `[]` |
| `(?<=b)` | `None` | `None` | `[]` |
| `a*` | `None` | `None` | `[]` |

Successful results retain `pos=2` and `endpos=1`; do not infer that every
zero-minimum-width pattern succeeds. Repeat these cases for bytes patterns and
subjects, and compare with equal bounds (`pos=endpos=1`) and independent
clamping (`pos=99`, `endpos=1` gives an empty-pattern match at `(3, 3)`, with
result bounds `3, 1`). Add these to engine/binding differential tests when
those implementations land; this remains a design-only document.

Cover bytes and Unicode category errors, all available subject widths, captures
across failed alternatives, empty repetitions, assertions, atomicity, case
behavior, search bounds, zero-width iteration, and mutable-buffer behavior
where supported. Class tests must cover byte bitmaps, compressed page
boundaries and shared pages, ranges, categories and their unions, negation with
case-insensitivity, supplementary characters, and lone surrogates across
available Unicode subject widths.

Performance comparisons should distinguish compilation from execution and
measure short matches, long searches, simple repetitions, capture-heavy
patterns, and substantial backtracking failures. Compare against CPython and,
where semantics permit, PCRE2 interpreter/JIT. Check semantic equivalence
before interpreting timing differences. No performance results have been
established.

## Execution Trace Examples

These worked traces apply the agreed context fields and transition table.
Reference outcomes were checked on local CPython 3.14.5. Iteration counts and
restoration steps are manual control-flow traces, not implemented-engine tests
or an exhaustive correctness proof. Preserve them as differential-test inputs
when the corresponding matcher features land.

### Nested Greedy Repeat Trace

Manually trace `fullmatch("((a|ab){1,2}){1,2}c", "abac")`. Call the outer
invocation O and the first inner invocation I. Subject positions are
`0:a 1:b 2:a 3:c 4:end`. Decision counts below belong to suspended contexts;
an invocation record also retains the prior value needed to reconsider a body.

| Step | Execution and continuation | Required state |
|---|---|---|
| 1 | O decides at count 0, then I decides at count 0; each requires a body | O remains owned while I is active; each has its own record and `AfterRepeat` owner. |
| 2 | I's branch chooses `a`, reaching position 1 and decision count 1 | The decision remembers `previous_count = 0`; group 2 currently spans `(0,1)`. |
| 3 | Greedy I attempts a second body at `b`; both arms fail | `AfterGreedyRepeatBody` restores captures, progress guard, and I's count to 0, then calls I's tail with O active. |
| 4 | I's tail closes group 1 at 1 and reaches O's decision count 1 | I is inactive but still owned and suspended. O attempts a second body at `b`; its newly created inner invocation fails its mandatory body and is released. |
| 5 | O's optional-body continuation restores its save/count, then tries tail `c` at position 1 | This fails. `AfterGreedyRepeatTail` restores O as active and its prior count 0, returning failure through I's tail. |
| 6 | I's tail continuation restores I as active and its prior count 0, returning failure to the earlier `a` arm | `AfterAlternative` restores its capture prefix/metadata and retries `ab` from position 0. Group 1's end written in the failed tail must also be restored. |
| 7 | `ab` reaches position 2; I's ending computes decision count 1 again | It must not compute 2 from a count left behind by the failed path. Its optional second body matches `a`, reaching count 2 at position 3. |
| 8 | At maximum, I calls its tail with O active; group 1 closes at 3 and O reaches count 1 | O's optional next body at `c` fails. Restore its capture save and try the outer tail at position 3; `c` succeeds at 4. |
| 9 | Success propagates through the suspended tail/body/branch handlers | Forward endpoint 4, discard saves without restoring live captures, restore optional progress guards, and release I then O through their owners. |

The result is whole span `(0,4)`, group 1 `"aba"` at `(0,3)`, group 2 `"a"`
at `(2,3)`, and Python `lastindex == 1`, verified on CPython 3.14.5. The fully
lazy variant `((a|ab){1,2}?){1,2}?c` on the same input yields both groups `"a"`
at `(2,3)`; its separate tail-first trace follows below. The
greedy pattern fails on `abbc`, also verified. These checks establish reference
outcomes, not exhaustive proof of all continuation interactions.

### Nested Lazy Repeat Trace

Trace `fullmatch("((a|ab){1,2}?){1,2}?c", "abac")`, with both repeats lazy.
Again O is the outer invocation and I the first inner invocation; a later
outer iteration creates a distinct inner invocation J. Subject positions remain
`0:a 1:b 2:a 3:c 4:end`.

| Step | Execution and continuation | Required restoration or ownership |
|---|---|---|
| 1 | Mandatory first bodies enter O and I; I's branch selects `a`, reaching position 1 and decision count 1 | Neither initial decision consumes input or increments a completed count. |
| 2 | Lazy I first calls its tail with O active, closing group 1 at 1 | I retains its record. Its tail attempt saves capture metadata and a prefix because O is active. |
| 3 | O reaches decision count 1 and first tries its tail `c` at position 1 | `c` fails at `b`. `AfterLazyRepeatTail` restores O as active and restores capture metadata; no prefix was copied for this tail, since O has no enclosing repeat. |
| 4 | O schedules its optional second body at position 1 | A new inner invocation requires a body at `b`; neither arm matches. Release that invocation. O's `AfterLazyRepeatBody` restores its old progress guard and count 0, then fails outward without an extra capture restore. |
| 5 | This failure returns to I's `AfterLazyRepeatTail` | Restore I as active and restore/consume its saved prefix and metadata, including the group 1 end written in its failed tail. I can now attempt an optional second body at position 1. |
| 6 | I's optional body also fails at `b` | `AfterLazyRepeatBody` restores I's progress guard and prior count 0. Failure reaches the earlier branch that chose `a`; `AfterAlternative` restores its own capture save before trying `ab` at position 0. |
| 7 | `ab` reaches position 2 and I again reaches decision count 1 | I tries its tail immediately, not another inner body. That closes group 1 as `ab` and reaches O's decision count 1 again. |
| 8 | O first tries `c` at position 2; it fails at `a` | Restore O's tail-attempt metadata, then schedule O's optional second body at position 2 with its progress guard saved/set. |
| 9 | This outer body creates J, whose mandatory first body chooses `a` | Position is 3; J reaches decision count 1 and tries its tail with O active. It does not first try another J body. |
| 10 | J's tail closes group 1 at `(2,3)` and reaches O's decision count 2 | O is at maximum and calls its tail. `c` succeeds at position 3, giving endpoint 4. |
| 11 | Success returns through the suspended handlers | Forward endpoint 4; discard capture saves rather than restoring them, restore optional progress guards, release J, then I, then O at their corresponding owners. |

The result is whole span `(0,4)`, both captures `"a"` at `(2,3)`, and
`lastindex == 1`. Unlike the greedy trace, the successful path uses two outer
iterations, each with one inner iteration. Capture group 1 therefore describes
the second outer body rather than the entire `aba` prefix.

An unsuccessful lazy body does not acquire a universal capture rollback rule:
step 4's changes are restored by I's suspended tail save, and step 6's changes
by the earlier branch's save. Both repeat records must have their prior counts
restored before that branch can retry. Optional guards are restored on both
successful and failed body returns; the failed attempt at position 1 cannot
block the later valid attempt at position 2.

Further local reference checks: this lazy pattern fails on `abbc`, yields both
groups `a` on `ac`, and yields groups `("aa", "a")` on `aaac`. They confirm
reference results, not the correctness of an implemented context machine.

### Empty Iteration Traces And Progress Guards

For empty subject and a body `()`, the control-flow traces are:

| Pattern | Iteration trace |
|---|---|
| `(){2,4}` | Two mandatory empty iterations, then one optional empty iteration; its next decision sees the guard at the same position and tries the tail. |
| `(){2,4}?` | Two mandatory empty iterations, then the tail succeeds without an optional attempt. |
| `(){2,4}+` | Two mandatory empty iterations, then one optional empty success; unchanged position stops the possessive loop. |

All three fullmatch the empty subject in CPython 3.14.5. Iteration counts above
come from the control-flow trace; returned captures alone do not expose them.
Possessive optional stopping compares the local body's successful endpoint to
its saved starting position, using the existing context fields. The first
optional attempt remains permitted after empty mandatory iterations.

For `fullmatch("((?:)|a)*a", "aa")`, the greedy body first chooses empty at
position 0 with guard 0. Another iteration is blocked, so tail `a` reaches 1;
fullmatch rejects that endpoint. Failure can revisit the existing body's
branch and choose consuming `a` instead. At position 1, a new optional body is
permitted; it chooses empty with guard 1, then the tail succeeds at 2. The
capture is empty at `(1,1)`. The guard blocks scheduling a new body, not
reconsidering alternatives inside the existing one.

The lazy `((?:)|a)*?a` first tries the tail at 0 and fails fullmatch at 1.
Its optional empty body leads to another lazy decision at 0; that decision
tries the tail before the guard blocks further body calls. Failure then
revisits the existing body's consuming alternative, reaches 1, and lets the
tail succeed at 2. The capture is `a` at `(0,1)`.

The possessive `((?:)|a)*+a` commits its first empty body and stops. Its tail
cannot fullmatch `aa`, and it cannot reopen the committed body's `a` arm.
These three fullmatch outcomes were verified locally. Additional probes:
`(a?)*` and `(a?)*+` on `a` retain a final empty capture at `(1,1)`, whereas
`(a?)*?` retains `a` at `(0,1)`. The ordinary `((?:)|a){2}a` fullmatches `aa`,
but its possessive `{2}+` counterpart fails. No new context fields are required.
The complete matcher must still verify these restoration paths under nesting.
See the pinned reference [ordinary progress guard](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L1088)
and [possessive optional loop](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h#L1227).

### Mixed Greedy And Lazy Repeat Traces

Compare the two mixed variants on `abac`, with O the outer invocation and I
its first inner invocation. These manual traces use the same fields and
transition table as the homogeneous greedy/lazy traces.

For inner greedy, outer lazy, `((a|ab){1,2}){1,2}?c`:

1. I's mandatory body chooses `a` and reaches position 1. Greedy I attempts
   another inner body at `b`; it fails. Restore its optional save/guard/count
   and call I's tail with O active.
2. O reaches decision count 1. Lazy O first tries final `c` at position 1;
   it fails. Restore O's tail-attempt metadata, then try another outer body.
   That body's mandatory inner match at `b` also fails; release its record.
3. O's lazy-body failure restores its prior count/guard and returns to I's
   greedy-tail continuation. Restore I's active selection/count, then return
   failure to the earlier branch, which restores captures and retries `ab`.
4. `ab` reaches position 2. Greedy I tries another body before its tail;
   `a` succeeds, reaching I count 2 at position 3.
5. I is at maximum and calls its tail with O active. O reaches count 1 and
   tries final `c` first. It succeeds; no second outer body is needed.

The result is group 1 `aba` at `(0,3)`, group 2 `a` at `(2,3)`: one outer
iteration containing two inner iterations.

For inner lazy, outer greedy, `((a|ab){1,2}?){1,2}c`:

1. I's mandatory body chooses `a` and reaches position 1. Lazy I tries its
   tail immediately, closing group 1 and reaching O's count-1 decision.
2. Greedy O first attempts a second outer body at `b`; its mandatory inner
   match fails. Restore O's optional save/guard/count, then try final `c` at
   the same position; it also fails.
3. Return to I's lazy-tail continuation. Restore its active selection and
   tail capture save, then try a second inner body at `b`. That fails too.
   Restore I's count/guard and return to the earlier branch, which retries
   `ab` after restoring its capture state.
4. `ab` reaches position 2. Lazy I again tries its tail first, reaching O's
   count-1 decision. Greedy O tries another outer body before final `c`.
5. This new inner invocation J matches mandatory `a` at position 2. Lazy J
   tries its tail at 3, closing group 1 and reaching O's count-2 decision.
   O is at maximum; its tail matches `c` and succeeds at 4.

The result is both groups `a` at `(2,3)`: two outer iterations, each containing
one inner iteration. Both patterns have whole span `(0,4)` and `lastindex == 1`.
All results were checked on local CPython 3.14.5. Both variants fail on `abbc`.
On `aaac`, inner-greedy/outer-lazy yields groups `(a,a)`, while
inner-lazy/outer-greedy yields `(aa,a)`.

Greediness is local to the decision that is currently executing; there is no
global preference propagated across nesting. Inactive invocation records stay
owned, count/guard restoration uses the corresponding continuation's locals,
and capture rollback can be performed by a suspended enclosing handler. These
traces do not establish implemented correctness or require new fields.

### Assertions Inside Repeat Traces

For `fullmatch("(?:(?=(a))ab)*ac", "abac")`:

1. The first optional body starts at 0. Positive lookahead captures `a` at
   `(0,1)` and returns local success with acceptance checks disabled. Its
   parent keeps captures and restores position 0; body `ab` reaches 2.
2. The repeat attempts another body at 2, preserving its current captures.
   Positive lookahead succeeds again, replacing group 1 with `(2,3)`; restore
   body position 2, consume `a`, then fail `b` against `c` at 3.
3. The assertion has already returned; there is no assertion-local rollback
   pending. `AfterGreedyRepeatBody` restores its own saved capture prefix,
   metadata, prior count, and progress guard, restoring group 1 to `(0,1)`.
4. The repeat tries tail `ac` at its saved position 2. It succeeds at endpoint
   4. Earlier successful paths discard their saves rather than restoring them.

The result is group 1 `a` at `(0,1)`, not the capture from the failed attempt.
The lazy-repeat variant `(?:(?=(a))ab)*?ac` gives the same capture: its first
tail at 0 fails, one body matches `ab`, and the next tail at 2 succeeds without
another body. A separate repeated lookahead probe, `(?:(?=(a))a)+b` on `aab`,
retains group 1 at `(1,2)` after the final assertion attempt fails at `b`.

For `fullmatch("(?:(?!(a)b)a)*ab", "aab")`:

1. At 0, negative assertion body `(a)b` captures `(0,1)` but fails `b`
   against the second `a`. `AfterNegativeAssertion` restores its metadata and
   saved prefix, then continues from original position 0. Body `a` reaches 1;
   group 1 remains unmatched.
2. The next optional repeat body starts at 1 with its repeat-level save.
   Negative assertion body captures `(1,2)` and matches `b` at 2, so the local
   child succeeds. `AfterNegativeAssertion` discards its own save and fails
   outward, without first restoring the live capture.
3. `AfterGreedyRepeatBody` receives that failure and restores its repeat-level
   save, making group 1 unmatched again. Its count/guard and position are also
   restored for the tail attempt. Tail `ab` from 1 succeeds at endpoint 3.

The result has group 1 unmatched and `lastindex is None`. Both successful and
failed negative-assertion body paths are therefore covered. The restored prefix
can have zero length here; restoring validity metadata must still hide newly
written slots. Another probe, `(?:(?!(a)c)a)+b` on `aab`, also succeeds with
group 1 unmatched after each failed negative body restores its own state.

Retain commitment of successful assertion bodies: on `abac`,
`(?:(?=(a|ab))\\1)+c` fails, whereas `(?:(?=(ab|a))\\1)+c` succeeds with group 1
at `(2,3)`. The first assertion chooses `a`; later repeat/tail failure cannot
reopen it to choose `ab`. Returning local success removes those internal
continuations while retaining captures.

Lookbehind uses the same return transitions with a shifted child start. On
`ab`, `a(?:(?<=(a))b)+` retains `(0,1)`. At position 0, insufficient preceding
input makes `(?:(?<=(a))b)*ab` skip its failed optional body and match the tail
with group 1 unmatched. Conversely `(?:(?<!(a))a)*b` initially passes its
negative lookbehind without a child call; at 1 its negative body succeeds and
the enclosing repeat restores that failed optional attempt, again leaving
group 1 unmatched. All three outcomes were checked locally.

No new fields or generic assertion capture checkpoint are required. Positive
assertion success preserves captures; negative-body failure restores its local
save; failed outward paths rely on the appropriate enclosing restoration.
Only ordinary failure is inverted, never allocation errors or interruption.

### Failed Possessive Attempt Traces

Separate three ordinary failure paths: an optional body that stops repetition,
a mandatory body that fails the repeat, and a tail that fails after repetition
has committed.

For `fullmatch("(?:(a)b)*+ac", "abac")`:

1. Count starts at 0. Save the optional-attempt capture state and call the
   local body at 0. Group 1 captures `(0,1)` and `b` matches at 1; local
   `SUCCESS` returns endpoint 2 without whole-pattern acceptance checks.
2. `AfterPossessiveOptionalBody` discards the save, adopts position 2, and
   increments count to 1. The completed body's contexts are gone; only current
   captures and the invocation record remain for the next iteration.
3. Save captures again and call the next body at 2. Group 1 becomes `(2,3)`,
   but `b` fails against `c` at 3. That failed child's endpoint is irrelevant.
4. The optional-body continuation restores its saved capture prefix/metadata,
   restoring group 1 to `(0,1)`. Generic return already restored its pre-attempt
   position 2. Count remains 1; this failed attempt is not an iteration.
5. Restore enclosing repeat selection, release the invocation record, and
   dispatch tail `ac` from 2. It succeeds at 4, retaining the earlier capture.

The result is group 1 `a` at `(0,1)`. On `ac`, the first optional attempt
partially captures `a` then fails `b`; restoring initial capture validity
makes group 1 unmatched before tail `ac` succeeds from 0. The bounded form
`(?:(a)b){1,3}+ac` on `abac` also retains group 1 at `(0,1)` after its failed
optional attempt.

For mandatory failure, `fullmatch("(?:(a)b){2}+ac", "abac")`:

1. The first mandatory body commits `ab`, count 1, and capture `(0,1)`.
2. The second mandatory body captures `(2,3)` but fails `b` against `c`.
3. `AfterPossessiveMandatoryBody` restores enclosing selection, releases the
   record, and fails outward. It does not try the tail or reopen the first
   iteration, and it does not introduce a local capture rollback rule.

There is no match. In `(?:(?:(a)b){2}+ac|abac)` on the same subject, the outer
`AfterAlternative` restores its own saved capture metadata before the literal
fallback succeeds. Group 1 is unmatched. That demonstrates ownership of
restoration on a failed outward path.

For committed tail failure, `fullmatch("(?:(a)b)*+ab", "abab")`:

1. Two successful optional bodies commit both `ab` pairs, count 2 at position 4.
2. The next optional body fails at end of input. Restore that attempt's saved
   captures/position, then release the repeat record and dispatch the tail.
3. Tail `ab` fails at 4. No repeat-body continuations remain to give back the
   second pair. The possessive path fails.

The ordinary greedy `(?:(a)b)*ab` succeeds instead, retaining group 1 at
`(0,1)`. Adding an outer fallback, `(?:(?:(a)b)*+ab|abab)`, lets that fallback
succeed with group 1 unmatched; commitment does not disable outer alternatives.

Alternatives remain available inside an uncompleted possessive iteration.
For `(?:(a|ab)c)*+ab` on `acab`, the first body commits `ac`. At position 2,
the next body tries `a` then fails `c`, retries `ab` then fails `c` at end of
input, and only then returns body failure. The possessive handler restores its
optional save, retaining the first capture `(0,1)`, and tail `ab` succeeds
from 2. The active repeat record must remain installed during those branch
attempts so their reference capture-preservation conditions apply.

Further checked outcomes: `(?:(a)b)*+a(c)` on `abac` retains group 1 at
`(0,1)` and group 2 at `(3,4)` with `lastindex == 2`; the positive-assertion
body `(?:(?=(a))ab)*+ac` retains `(0,1)`. These combine failed attempts with
metadata updates and captures written by a successful local assertion.

No new fields or rollback mechanism are needed. Optional failure restores its
own attempt and continues to the tail; mandatory failure cleans up and fails
outward; tail failure cannot reopen committed iterations. Errors take none of
those ordinary failure paths and instead abort the invocation.

## References

- [CPython Unicode name representation and lookup](https://github.com/python/cpython/blob/v3.14.5/Modules/unicodedata.c)
- [CPython Unicode database generator](https://github.com/python/cpython/blob/v3.14.5/Tools/unicode/makeunicodedata.py)
- [CPython 3.14 `re` documentation](https://docs.python.org/3.14/library/re.html)
- [Pattern parser](https://github.com/python/cpython/blob/v3.14.5/Lib/re/_parser.py)
- [Bytecode compiler](https://github.com/python/cpython/blob/v3.14.5/Lib/re/_compiler.py)
- [Compiled pattern, match, and execution state](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre.h)
- [Matcher and structured instruction handlers](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre_lib.h)
- [Width instantiations and Python binding](https://github.com/python/cpython/blob/v3.14.5/Modules/_sre/sre.c)
- [CPython regex tests](https://github.com/python/cpython/blob/v3.14.5/Lib/test/test_re.py)

Source links pin CPython v3.14.5. Record the exact adapted revision in generated
data/code and retain applicable license notices and attribution for derived
code and tests.
