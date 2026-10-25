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

The document follows the compilation and execution pipeline:

- [Architecture](#module-boundary) and [pattern storage](#pattern-categories-and-subject-storage).
- [Parser and compiler](#parser-and-compiler-representation), [Unicode data](#shared-unicode-data), and [program ownership](#compiled-program-and-builder).
- [Bytecode and execution](#bytecode-and-execution), including backtracking and class storage.
- [Search](#search-metadata), [result objects](#pattern-and-match-objects), [iteration](#iteration-and-empty-match-progression), and [replacement](#replacement-programs-and-substitution).
- [Errors](#error-behavior), [open decisions and verification](#open-decisions-and-verification).

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
API-specific output rules remain to be settled.

Replacement-template compilation and interpretation are C++, following the
separate [replacement program](#replacement-programs-and-substitution).
Callable replacements use the ordinary runtime Python call API; detailed
callback behavior remains a separate design decision.

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
contracts and remains deferred to integration. Native-resource cleanup and
concrete binding APIs must implement the ownership model described here.

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
access, and character operations. Exact template parameters and type names
remain open. Separate literal widths do not require separate opcode definitions
or an 8-bit instruction format for bytes programs.

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
builder. Freeze validates and publishes the program, then compilation discards
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
validation requires a fixed predicate distinct from regex word membership; its
table integration remains to be specified.

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
    [[nodiscard]] Result<std::unique_ptr<CompiledProgram<Literal>>, CompileError>
    freeze() &&;

private:
    std::unique_ptr<CompiledProgram<Literal>> program_;
    // Compile-only fixups and deduplication state.
};
```

This is a design sketch, not a selection of a concrete error-result API.
Instantiate the literal storage with `uint8_t` for bytes and `uint32_t` for
Unicode. The remaining owned payloads include inclusive 32-bit range endpoint
pairs, the shared `uint64_t` bitmap-word pool, and the `uint32_t` page
directory. Typed class/payload accessors follow the same const-element pattern.
Program metadata belongs to the program; mutable compiler bookkeeping belongs
to the builder, and execution scratch belongs to each match invocation.

The friend builder creates and fills an unpublished program. Its consuming
`freeze()` resolves fixups, finalizes pools and metadata, checks format limits,
and validates instruction boundaries, targets, and referenced slices before
transferring ownership of the completed object. On failure it publishes no
program. After success, the builder retains no writable aliases into the
program, and execution never mutates its storage. Freezing does not copy or
pack the vectors into another allocation.

Native container sizes remain host-sized internally; conversions into the
engine's 32-bit counts and indices must be checked against the agreed limits.
The Python `Pattern` object owns the completed program, as specified below; the
unique pointer above expresses the builder's ownership transfer.

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

Each instruction below occupies one word, with an 8-bit opcode in bits 0-7 and
the indicated payload in bits 8-31.

| Instruction | 24-bit payload | Behavior |
|---|---|---|
| `SUCCESS` | Unused | Complete the whole pattern successfully, subject to the caller's match-mode and progress requirements |
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
storage width. Predicate and test identifiers are specified below; `AT` numeric
assignments remain open.

Capture slots are paired: slots 0 and 1 record Python group 1's start and end,
slots 2 and 3 record group 2, and so on. `GROUPREF` uses zero-based group
indices internally. The entire-match span is tracked separately. `SUCCESS` is
exclusively whole-pattern success: regions finish with `REGION_END`, and
general repeat bodies finish with `REPEAT_END`. Caller requirements such as
full-match end position or zero-width iteration progress can reject a candidate
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
where practical. The numeric operation-family assignments remain open.

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
| Execution stack | `UINT32_MAX` entries |
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
`GROUPREF_EXISTS_WIDE` and literal-run wide forms have two full operands and
therefore use three words, as specified in their own sections. Numeric
assignments are not yet finalized. Overflow/resource errors follow the policy
below.

### Position Tests

`AT` is one word: opcode in bits 0-7 and a test identifier in bits 8-31. The
compiler resolves effective scoped flags into that identifier. No ignorecase
variants or runtime flag stack are needed. Numeric identifier assignments are
not yet frozen.

| Identifier | Condition | Source syntax |
|---|---|---|
| `BEGIN_STRING` | Position is zero in the original subject | `\A`, or `^` without `MULTILINE` |
| `BEGIN_LINE` | Position is zero or the preceding character is `\n` | `^` with `MULTILINE` |
| `END_STRING` | Position equals effective subject end | `\z`, `\Z` |
| `END_OR_FINAL_NEWLINE` | At effective end, or immediately before its final `\n` | `$` without `MULTILINE` |
| `END_LINE` | At effective end, or the next character is `\n` | `$` with `MULTILINE` |

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
| `GROUPREF_EXISTS` | One word: opcode in bits 0-7, zero-based group index in bits 8-15, absolute false-target word index in bits 16-31 |
| `GROUPREF_EXISTS_WIDE` | Three words: opcode word with upper 24 bits zero; 32-bit zero-based group index; 32-bit absolute false target |

If the group participated, fall through after the entire encoding; otherwise
jump to the false target. An empty capture counts as participation and must
remain distinguishable from an unmatched group. Re-evaluate the current capture
state whenever the instruction is reached, including after outer backtracking.

The compact form covers group indices 0-255 and targets 0-65535. Use the wide
form when either operand exceeds its compact field. Larger group-index policy
remains tied to the overall capture/mark limits. For `(a)?(?(1)b|c)`, the
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

### Failure Continuations And Jumps

Use `TRY` and `JUMP` for ordered alternatives and ordinary control transfer.
Capture and context restoration use the tagged undo stack described below.

| Instruction | Encoding |
|---|---|
| `TRY`, `JUMP` | One word: opcode in bits 0-7, absolute target word index in bits 8-31 |
| `TRY_WIDE`, `JUMP_WIDE` | Two words: opcode in bits 0-7 with upper 24 bits zero; 32-bit absolute target word index in the second word |

Targets count 32-bit words from the start of the instruction stream and must
identify an instruction's first word, never an operand word. Both forward and
backward transfers use the same unsigned absolute representation. Use the
compact form for targets through `0xFFFFFF` and the wide form otherwise.
Internal program counters are `uint32_t`, matching the stack-record fields. The
compiler patches targets after final instruction layout; no native pointers or
signed relative displacements occur in these operands.

- `TRY target` saves a failure continuation and a matching-state checkpoint,
  then falls through to the next instruction. It does not immediately transfer
  control to its operand.
- `JUMP target` transfers control without saving a continuation or checkpoint.

Fallthrough after `TRY` is the next instruction after its entire encoding: one
word for the compact form and two for the wide form. Wide and compact
instructions otherwise have identical semantics.

On failure, the executor unwinds to an available failure continuation, restores
its matching-state checkpoint, and resumes at its target. This resembles a call
with a continuation activated by failure rather than normal return. Passing a
`JUMP` or successfully matching the first alternative does not commit that
choice: a later failure can still activate the saved continuation. Atomic
groups and possessive repetitions need explicit rules for discarding their
internal alternatives.

For illustration, the matching control flow for `(ab|a)b`, with capture marks
omitted, is:

```text
    TRY alternative
    LITERAL_RUN "ab"
    JUMP continuation
alternative:
    LITERAL 'a'
continuation:
    LITERAL 'b'
    SUCCESS
```

The labels above stand for absolute instruction-word indices. Against `ab`, the
first path consumes `ab` and then fails on the final `b`. Restoring the
checkpoint allows the second path to match `a` followed by `b`.

Checkpoint restoration must preserve subject position, capture boundaries and
participation, last-captured-group metadata, and relevant enclosing execution
state. Capture changes after alternatives rejoin can also need rollback; saving
only captures syntactically inside the first alternative is insufficient.

### Backtracking Stack And Capture Undo

Use the Onigmo-style mechanism: one growable vector of fixed-size tagged
records containing alternatives, mutation-undo records, and structured boundary
records. Python capture semantics remain the reference; the storage/unwind
mechanism is adapted from Onigmo. No performance advantage is claimed without
measurements.

Current capture marks live in a flat array of subject indices, not references
to historical stack records. Before changing a mark, push its previous value
and the previous last-captured-group metadata. Save one mark per undo record,
rather than the entire group pair or all existing captures. Record the metadata
even when that particular mark write does not change it; eliminating redundant
saves is a later optimization.

The stack carries alternatives, capture restoration, repeat contexts, and
assertion/atomic boundaries:

```cpp
enum class StackEntryKind : uint8_t
{
    Alternative,
    DisabledAlternative,
    RestoreMark,
    PositiveAssertion,
    NegativeAssertion,
    AtomicGroup,
    ClosedRegion,
    RepeatOneRetry,
    DisabledRepeatOneRetry,
    Repeat,
    RestoreRepeat,
    RestoreActiveRepeat,
};

struct AlternativeRecord
{
    int64_t subject_pos;
    uint32_t resume_pc;
};

struct RestoreMarkRecord
{
    int64_t previous_position;
    uint32_t slot;
    int32_t previous_last_group; // -1 means no captured group
};

struct RegionRecord
{
    int64_t subject_pos;
    uint32_t previous_region;
    uint32_t entry_repeat;
    uint32_t continuation_pc;
};

struct RepeatOneRetryRecord
{
    int64_t next_pos;
    int64_t limit_pos;
    uint32_t repeat_pc;
};

struct RepeatRecord
{
    uint64_t count;
    uint64_t maximum;
    int64_t last_optional_pos;
    uint32_t previous_active_repeat;
    uint32_t repeat_pc;
};

struct RestoreRepeatRecord
{
    uint64_t previous_count;
    int64_t previous_last_optional_pos;
    uint32_t repeat_index;
};

struct RestoreActiveRepeatRecord
{
    uint32_t previous_active_repeat;
};

struct StackEntry
{
    StackEntryKind kind;
    union
    {
        AlternativeRecord alternative;
        RestoreMarkRecord restore_mark;
        RegionRecord region;
        RepeatOneRetryRecord repeat_one_retry;
        RepeatRecord repeat;
        RestoreRepeatRecord restore_repeat;
        RestoreActiveRepeatRecord restore_active_repeat;
    };
};

// Per-match scratch storage:
// absl::InlinedVector<StackEntry, kInlineStackEntries> stack;
// absl::InlinedVector<int64_t, kInlineMarkSlots> marks;
```

Use inline-capacity vectors for the backtracking stack and live capture marks
so small matches do not require heap allocation for those buffers. Follow the
existing `absl::InlinedVector` pattern in CloverVM. The symbolic capacities
above are not yet selected; choose them against the final stack-entry size and
typical capture counts. Larger matches spill to dynamically allocated storage
without changing semantics. This does not promise allocation-free pattern
compilation or Python match-object construction. Parser/compiler scratch
collections can also use inline storage where typical sizes justify it; do not
embed a separate allocating container in every stack entry.

An unmatched mark uses -1, distinct from every valid nonnegative subject
position, including the end position. Absent stack/context links use the
separate unsigned `UINT32_MAX` sentinel. Validate subject positions and
internal slot, group, and program indices against their respective limits.
`previous_last_group` is a zero-based group index or -1, converted to Python's
group numbering at the binding boundary.

`TRY` pushes an `AlternativeRecord` containing the failure target and current
subject index. It does not copy captures or save a generic execution-checkpoint
token. `MARK` pushes a `RestoreMarkRecord` before modifying its slot and, for a
closing mark, updating the last-group metadata. Every mark starts unmatched.

Failure pops records in reverse order. A mark-undo record restores its slot and
last-group metadata. A disabled alternative is skipped. An enabled alternative
restores the subject index and resumes execution at its target. If no usable
alternative remains, the current matching attempt fails. This is an unwind
operation, not just a vector resize. Other record kinds must define their own
unwind actions before being added.

The tag and union payloads contain plain indices and values, with no per-entry
heap allocation. All cross-record references use stable indices/offsets rather
than pointers into the vector. Overall element size is determined by the
largest payload plus tag/alignment. A bounded local Clang layout experiment
measured `RepeatRecord` at 32 bytes, `RestoreRepeatRecord` at 24 bytes,
`RestoreActiveRepeatRecord` at 4 bytes, and the combined `StackEntry` at 40
bytes. These are observations on the current target, not a portable packed ABI.

The `uint64_t` completed-iteration count belongs in a separate repeat-context
record on the shared stack, not in each `TRY` alternative record. Each dynamic
invocation has its own repeat context, including nested or repeated entry into
the same compiled body. Store the normalized `uint64_t` maximum in that context
too; it is fixed for the invocation and need not be logged on every state
change. For repetition, save previous mutable values, including the full 64-bit
count and empty-match progress state where changed, instead of relying only on
inverse operations such as decrement.

Initialize `count` to zero and `last_optional_pos` to -1. `repeat_pc`
identifies the immutable entry instruction and therefore its minimum, mode,
body, and exit. `previous_active_repeat` links to the enclosing context or
`UINT32_MAX`. Keep the minimum in bytecode rather than duplicating it in the
context.

`last_optional_pos` records where an optional iteration was last scheduled. Use
it to prevent repeated optional attempts without input progress, while
mandatory iterations below the minimum bypass that restriction. Before changing
either count or progress position, push `RestoreRepeatRecord` with both old
values. Popping restores them in the context identified by `repeat_index`.
Maximum and linkage fields are immutable and do not need mutation logging.

Entry pushes `RepeatRecord` and makes it active. Ordinary `REPEAT_EXIT` pushes
`RestoreActiveRepeatRecord`, switches to the enclosing active repeat, and
leaves the context alive below retained alternatives and undo records. Popping
an active-selection undo record restores its saved index. Finally popping a
repeat context restores active selection from that context's enclosing-repeat
link. LIFO ordering ensures a repeat context remains alive while any undo
record or alternative above it can refer to it. Vector growth requires
reacquiring references by index after a push.

For greedy optional iteration, save the exit alternative before updating
progress for the next body attempt; failing that attempt must restore the
pre-attempt state before activating the saved exit. For lazy optional
iteration, prepare progress state before saving the body alternative, so
resuming it sees the intended state. Count-update undo records precede their
increments, ensuring backtracking into an earlier body's alternatives restores
the pre-completion count. Preserve these ordering rules through nested regions
and repeat contexts.

### Assertion And Atomic Regions

The agreed entry instructions are `ASSERT`, `ASSERT_NOT`, and `ATOMIC_GROUP`,
with corresponding `_WIDE` forms. All targets are absolute instruction-word
indices identifying the instruction after the body's `REGION_END`.

| Instruction | Encoding |
|---|---|
| `ASSERT`, `ASSERT_NOT` | Two words: opcode in bits 0-7 with 24-bit continuation target in bits 8-31; 32-bit lookbehind distance in the second word |
| `ASSERT_WIDE`, `ASSERT_NOT_WIDE` | Three words: opcode word with upper 24 bits zero; 32-bit continuation target; 32-bit lookbehind distance |
| `ATOMIC_GROUP` | One word: opcode in bits 0-7 with 24-bit continuation target in bits 8-31 |
| `ATOMIC_GROUP_WIDE` | Two words: opcode word with upper 24 bits zero; 32-bit continuation target |
| `REGION_END` | One word with no payload; complete the body of the active region |

The body begins immediately after the complete entry encoding. Assertion
distance zero means lookahead; a positive distance means fixed-width
lookbehind. If the distance exceeds available preceding input, a positive
assertion fails and a negative assertion succeeds without executing the body.
Atomic entry does not move the subject position and has no distance operand.

For example, `(?=(ab))ab` has the symbolic layout:

```text
    ASSERT continuation, back=0
    MARK 0
    LITERAL_RUN "ab"
    MARK 1
    REGION_END
continuation:
    LITERAL_RUN "ab"
    SUCCESS
```

Use the shared `RegionRecord` payload above, tagged `PositiveAssertion`,
`NegativeAssertion`, or `AtomicGroup`. Its fields are:

| Field | Meaning |
|---|---|
| `subject_pos` | Original subject position before entering the body |
| `previous_region` | Stack index of the enclosing active region, or a sentinel |
| `entry_repeat` | Active-repeat index at entry, or a sentinel |
| `continuation_pc` | Instruction after the region |

Per-match execution keeps an `active_region` stack index. Entry pushes the
record and makes it active. The boundary's own stack index identifies the start
of the region's stack activity; there is no separate stack-size checkpoint. All
fields use indices, so inline-vector growth does not invalidate references.

| Region | Body reaches `REGION_END` | All body paths fail |
|---|---|---|
| Positive assertion | Keep captures, restore original subject position, continue | Fail outward |
| Negative assertion | Undo internal effects, restore position, fail outward | Restore position, continue |
| Atomic group | Keep captures and consumed position, continue | Fail outward |

`REGION_END` uses `active_region` to select the region's completion behavior;
it has no embedded region identifier or polarity operand. Exhausting a body
through failure does not execute `REGION_END`: the unwind loop reaches its
boundary record instead.

On successful positive assertion or atomic completion, scan above the boundary
and disable internal retry opportunities in place. This includes specialized
repeat retry records if introduced, not just `TRY` alternatives. Retain undo
records and any contexts they reference so later failure through an outer
alternative can still roll back the region's successful effects. Do not compact
the stack or blindly resize it.

Restore `active_region` to `previous_region` and active-repeat selection to
`entry_repeat`, change the boundary tag to `ClosedRegion`, and resume at
`continuation_pc`. Positive assertions restore `subject_pos`; atomic groups
retain the consumed position. A closed boundary is skipped during later
unwinding and must not perform assertion result inversion again. Retained
repeat contexts remain alive while referenced by their undo records.

If a negative assertion's body succeeds, unwind directly to its boundary,
applying mutation undo records and discarding retries without activating their
targets. Remove the boundary, restore its original position and enclosing
contexts, and fail outward. If normal backtracking exhausts its body instead,
the boundary catches that failure before an outer alternative can activate,
restores the entry state, and continues after the assertion. Failed positive
assertions and atomic bodies likewise remove their exhausted boundary and
propagate failure after internal undo. Only ordinary match failure is inverted
by a negative assertion; engine errors remain errors.

Lookahead and fixed-width lookbehind use the same region record. The lookbehind
distance belongs in bytecode. Save the original subject position before moving
back to the body's start; that saved position also identifies the required body
end on lookbehind success. Insufficient preceding input follows the polarity
rules above. Integration with final repeat/context payloads remains to be
verified against these region rules.

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
and negative predicates have separate bits: `[a\\D]` is explicit `a` OR
non-digit, while `[^a\\d]` negates explicit `a` OR digit. The metadata contains
no representation, case-insensitivity, or subject-width field. Opcodes select
representation and locale behavior; fixed case expansion is stored in the
membership data. Locale-ignorecase combines base membership for both mapped
bytes before applying outer negation once.

Class records contain data references, not a representation tag. A
category-only class can reference an empty bitmap and select its category
predicates. Payload offsets reference shared class-data storage; concrete pool
layouts/units are specified as those layouts are settled.

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
remains 32 bytes, but membership loads a 64-bit word. Bitmap offsets and
directory references remain 32-bit and count elements of the shared `uint64_t`
bitmap-word pool; instruction words remain 32-bit. The explicit page membership
test uses:

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

The full class combines this explicit membership with categories and outer
negation as specified below.

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

Membership is the union of explicit sets/ranges and category predicates, with
class negation applied to the final result. Case-insensitive membership must
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
`IN_LOCALE_IGNORECASE_BITMAP`, compute match-time locale lowercase and
uppercase byte values, combine base membership for both, and only then apply
outer negation. Base membership includes the bitmap and category predicates but
excludes the outer complement. This follows CPython's locale-class comparison
and avoids complementing a class before closing it under the case relation.

Locale-ignorecase literals and negated literals lower to these classes; no
separate locale literal or literal-run opcode is selected initially. Both
locale class instructions are eligible one-character tests for simple repeats.
Fixed members without locale-dependent predicates or ignorecase needs can still
use ordinary `IN_BITMAP`. `GROUPREF_LOCALE_IGNORECASE` supplies the separate
runtime backreference comparison.

Compiled class records remain immutable. Evaluate dynamic rules directly in the
initial design. A bitmap materialized using the matching-time locale is a
possible later optimization, not an unconditional property of the compiled
pattern; no locale-result cache or per-match materialization policy is
selected.

### General Repeat Instructions

The agreed general-repeat instruction family is:

| Instruction | Behavior |
|---|---|
| `REPEAT_GREEDY` | Create a repeat context and prefer another optional iteration |
| `REPEAT_LAZY` | Create a repeat context and prefer exit once the minimum is satisfied |
| `REPEAT_END` | Complete one successful body iteration and make the next repetition decision |
| `REPEAT_EXIT` | Leave the repeat and restore the enclosing active-repeat selection |

Compact entry instructions have three words:

```text
word 0: opcode in bits 0-7, absolute exit word index in bits 8-31
word 1: uint32_t minimum
word 2: uint32_t maximum
body follows
```

Use `REPEAT_GREEDY_WIDE` / `REPEAT_LAZY_WIDE` when the exit target needs more
than 24 bits: the opcode word has upper bits zero, followed by a full 32-bit
exit target, minimum, and maximum, for four words total. The target identifies
the corresponding `REPEAT_EXIT`, not its following tail instruction. Reserve
maximum `0xFFFFFFFF` for unbounded repetition; finite-bound validation must
respect the sentinel. Validate minimum/maximum ordering and representable
finite bounds. The dynamic `uint64_t` count always represents actual completed
iterations, including for unbounded repeats. Do not clamp it at the minimum or
otherwise saturate it.

At repeat entry, normalize the encoded maximum once:

```cpp
uint64_t maximum = encoded_maximum == UINT32_MAX
                       ? UINT64_MAX
                       : static_cast<uint64_t>(encoded_maximum);
```

Store that normalized maximum in the runtime repeat context. The ordinary
decision path then compares the count against the compiled minimum and the
normalized maximum, without repeatedly interpreting the bytecode sentinel.
Minimum remains available from the compiled instruction. Increment the counter
with checked arithmetic and save/restore its full width during backtracking.
Reaching the representational ceiling must report counter exhaustion rather
than wrap, saturate, or silently turn an unbounded repeat into a finite match;
the overflow/resource exception policy is specified below.

`REPEAT_END` and `REPEAT_EXIT` are one-word instructions with no payload. Their
active repeat context identifies the associated entry instruction. A repeated
body can contain arbitrary instructions, nested repeats, captures, and
assertions; it is not restricted to one character or one instruction.

For example, `(ab){2,5}` has the symbolic layout:

```text
    REPEAT_GREEDY exit, min=2, max=5
body:
    MARK 0
    LITERAL_RUN "ab"
    MARK 1
    REPEAT_END
exit:
    REPEAT_EXIT
    ... tail ...
```

Entry creates a context with count zero and makes the initial decision. After
each successful iteration, `REPEAT_END` saves the previous mutable repeat state
and increments the completed count. Below the minimum, another iteration is
required. At the maximum, exit. Between those bounds, greedy saves an exit
alternative and executes the body; lazy saves a body alternative and exits. The
zero-count entry decision permits immediate exit when the minimum is zero.
Empty-match progress rules constrain optional iterations without preventing
mandatory empty iterations needed to satisfy the minimum. Backtracking can
revisit choices inside an earlier iteration, not only remove whole iterations.

Saved exit alternatives target `REPEAT_EXIT`. That handler saves the current
active-repeat selection for undo, switches to the enclosing context, and falls
through to the tail. It leaves the inner context alive for retained
alternatives and undo records. Resuming a body alternative uses the existing
restored context, not the entry instruction that would create a new invocation.
Undo-record placement must preserve the state expected at each saved resume
target.

Initially lower general possessive repetition as a greedy repeat inside an
atomic region, reusing the agreed commit rules. A dedicated general possessive
instruction is a later performance candidate. The simple one-character fast
path below does not allocate a general repeat context per consumed character.

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

Greedy and lazy execution use the `RepeatOneRetryRecord` in the stack sketch.
`repeat_pc` identifies the entry instruction, mode, test, and tail. On retry,
decode the header and complete test again to recover the predicate and tail; do
not enlarge the retry record merely to cache the variable instruction extent.
For greedy retry, `next_pos` is the next shorter endpoint and `limit_pos` is
the shortest endpoint satisfying the minimum. Activating the record tries that
endpoint and pushes an updated record only if another shorter candidate
remains.

For lazy retry, `next_pos` is the endpoint after one additional character and
`limit_pos` is the furthest permitted by the maximum and subject bounds. On
activation, test the character immediately before `next_pos`. A failed test
discards the retry and continues unwinding; a successful test tries the tail
and retains a further retry if permitted. Do not pre-scan the entire potential
run for a lazy repeat. Compute endpoints with checked/clamped arithmetic.

Only one retry record for a given simple-repeat invocation is pending at a
time; it is popped before an updated one is pushed. This does not prohibit
multiple invocations of that instruction from having records alive together.
Possessive execution needs no retry record. Capture changes made by the tail
are undone before a repeat retry is activated through the ordinary stack order.
Atomic/assertion commit scans must disable these retry records too, using
`DisabledRepeatOneRetry`; that tag is skipped on unwind. General-repeat context
selection is unaffected by the simple handler.

## Search Metadata

Store a small `SearchInfo` record in the compiled program, with larger tables
in its owned vectors. Search metadata selects candidate starting positions for
`re.search`; the matcher executes the same program from each candidate.
`re.match` tries only at `pos`, while `re.fullmatch` also requires completion
at `endpos`. These entry points can still use the minimum-width rejection
check.

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
second word is zero. The builder derives and validates this metadata before
publication. No execution-time mutation is needed.

`minimum_width` is a conservative lower bound on consumed subject elements.
Reject candidates when fewer elements remain. Nested repeat bounds can produce
widths larger than 32 bits, so compute with checked, capped 64-bit arithmetic:
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
and retains the original pattern, global flags, and group-name metadata. Its
methods, including `search`, `match`, and `fullmatch`, execute that program
with private per-call matching state. Reusing a `Pattern` does not require a
cache.

`Match` is the returned match result, represented by retained references and
owned capture spans rather than a tuple of eagerly copied capture strings:

```text
Match
    pattern  -> Pattern
    subject  -> original string or bytes-like subject
    captures -> [(start, end), ...]
```

The result's span array includes group zero for the whole match, followed by
the numbered captures. This does not change the executor's separate handling of
whole-match boundaries and capture marks. Use the agreed signed 64-bit subject
offsets, with an absent span for an unmatched group. The `Match` owns its
result spans independently of the executor's mutable scratch state and retains
its `Pattern` and original subject for its lifetime.

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
capture spans and update:

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

Compile replacement templates into a separate program with a separate small
instruction set and interpreter. Replacement code is not stored in the regex
program, does not use matching opcodes, and is not called by regex bytecode. A
compiled `Pattern` may be reused with different replacement templates.

The replacement instruction set contains literal insertion and capture
insertion. Parse and validate a template against the `Pattern`'s capture count
and group-name metadata before searching, resolve names to numeric references,
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

1. Compile and validate the replacement against the pattern.
2. Create a search cursor and output builder.
3. For each match, append preceding unmatched subject text, then interpret the
   replacement program using that match's capture spans.
4. Append the remaining subject text and return the result, with the count for
   `subn`.

Process one match at a time; do not first collect the complete set of matches.
Search always examines the original subject, so inserted output is never
searched again. Search progression and the end of already emitted subject text
are separate positions, particularly around empty matches. Count limits and
API-specific argument/error ordering must follow CPython.

Callable replacements take a separate path: construct a `Match`, invoke the
callback through the runtime call API, and append the returned text without
interpreting it as a template. Callback/result validation and detailed ordering
remain to be worked out; callback exceptions propagate under the existing error
contract.

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

## Open Decisions And Verification

Remaining design decisions:

- Replacement callbacks: return-value validation, invocation order, and errors.
- API result rules: `findall` capture shapes, `split` separators and captures,
  unmatched groups, count limits, and argument/error ordering.
- Shared Unicode identifier validation for group names.
- Numeric opcode-family and `AT` test assignments, and complete structural
  validation rules. `CATEGORY` identifiers and operand layouts are settled.
- Concrete binding APIs, native-resource cleanup, and the initial syntax/API
  slice, including buffer-protocol dependencies. Execution-time subject-storage
  mechanics remain deferred to runtime integration.
- Final lowering details needed for the selected implementation slice.

Verification must establish nested repeat/region unwind ordering, specialized
retry decoding with compact/wide class operands, and search analysis/scanning.
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

## References

- [CPython Unicode name representation and lookup](https://github.com/python/cpython/blob/3.14/Modules/unicodedata.c)
- [CPython Unicode database generator](https://github.com/python/cpython/blob/3.14/Tools/unicode/makeunicodedata.py)
- [CPython 3.14 `re` documentation](https://docs.python.org/3.14/library/re.html)
- [Pattern parser](https://github.com/python/cpython/blob/3.14/Lib/re/_parser.py)
- [Bytecode compiler](https://github.com/python/cpython/blob/3.14/Lib/re/_compiler.py)
- [Compiled pattern, match, and execution state](https://github.com/python/cpython/blob/3.14/Modules/_sre/sre.h)
- [Matcher and structured instruction handlers](https://github.com/python/cpython/blob/3.14/Modules/_sre/sre_lib.h)
- [Width instantiations and Python binding](https://github.com/python/cpython/blob/3.14/Modules/_sre/sre.c)
- [CPython regex tests](https://github.com/python/cpython/blob/3.14/Lib/test/test_re.py)
- [Onigmo tagged stack, capture undo, and atomic handling](https://github.com/k-takata/Onigmo/blob/master/regexec.c)

These branch links describe the reference architecture; pin the exact CPython
revision used for an adaptation and retain applicable license notices and
attribution for derived code and tests.
