# Compact Heap Header: A/B Measurements

| Field | Value |
|---|---|
| Document type | Investigation |
| Status | Experimental |
| Implementation | Header representation implemented in the working tree; production forwarding disabled |
| Scope | Release performance, layout, and code-generation checks for the eight-byte contained HeapHeader |
| Owning layers | Object model and memory |
| Validated against | Initial comparison based on `8d3bb678`, 2026-10-31; generation-preserving revision based on `21303dce` and NonMoving revision based on `6ebce070`, 2026-11-01 |
| Supersedes | N/A |

## Method

A is the pre-implementation Release executable, saved before the header change.
B is the Release build of the standalone HeapHeader contained by HeapObject.
Both use the same checkout, benchmark inputs, build flags, and machine. Native
builds/tests/benchmarks were serialized under the shared build lock. Runtime JIT
tiering is disabled in this build; these are not production moving-GC results.

The entire 92-case suite was run once for each binary:

```sh
BENCH_BINARY --benchmark_min_time=1s --benchmark_counters_tabular=true \
  --benchmark_out=RESULT.json --benchmark_out_format=json
```

To check the first pass rather than cherry-pick its wins, 16 cases covering the
largest apparent changes plus construction, floats, and reclamation were rerun
in A1, B1, B2, A2 order. Each block used three repetitions at a 0.5-second minimum.
The repeat table compares medians of six CPU-time measurements per binary.

```sh
BENCH_BINARY --benchmark_filter=PATTERN --benchmark_min_time=0.5s \
  --benchmark_repetitions=3 --benchmark_counters_tabular=true \
  --benchmark_out=RESULT.json --benchmark_out_format=json
```

The exact filter was:

```text
^BM_(Mandelbrot|ForLoop|GetItemDictGeneral|SetItemUser|ClassInstantiationNoInit|ClassInstantiationWithInit|StrConstructorInt|FloatConstructorFloat|FloatConstructorInt|FloatConstructorString|InstanceAttributeWrite|MethodKeywordCall|MemoryReclamation|ExpandedCallCollectRest|ExpandedCallTupleDirect|FunctionKeyword)/[0-9]+$
```

All comparisons use CPU time. Improvement is `(before / after - 1) * 100`:
positive means faster; negative means slower. CV is sample standard deviation
divided by mean, in percent. Affinity/frequency metadata warnings and ordinary
machine noise remain; no confidence claim is made from the single full-suite
pass. In particular, the first pass's aggregate is not a blanket VM speedup.

## Results And Decision Boundary

The first full pass has a median improvement of +1.20% and an
unweighted geometric-mean improvement of +2.00%. Individual results are mixed.
The repeat medians are more useful for the selected cases: method keyword calls
are 4.48% slower and construction without __init__ is 2.91% slower, while memory
reclamation is 6.62% faster. Some large first-pass wins disappear on repetition.
Mandelbrot remains noisy (roughly 4.8% CV in both binaries), so its roughly 3%
repeat-median decline is not a clean isolated regression.

Review accepted the mixed profile as broadly neutral for this representation
change, with the individual slowdowns retained rather than hidden by an aggregate.
The pattern is consistent with code-layout effects, but the profiles do not
establish that cause. No speculative code-layout tuning was added.

## Interleaved Confirmation

| Benchmark | A median CPU (us) | B median CPU (us) | Improvement | A CV | B CV |
|---|---:|---:|---:|---:|---:|
| BM_Mandelbrot/100 | 27605.365 | 28455.074 | -2.99% | 4.88% | 4.74% |
| BM_ForLoop/100000 | 283.188 | 286.837 | -1.27% | 0.59% | 0.03% |
| BM_GetItemDictGeneral/100000 | 2028.470 | 1990.928 | +1.89% | 2.98% | 2.59% |
| BM_SetItemUser/100000 | 1080.290 | 1066.625 | +1.28% | 0.25% | 0.70% |
| BM_ClassInstantiationNoInit/100000 | 1716.164 | 1767.625 | -2.91% | 0.17% | 0.86% |
| BM_ClassInstantiationWithInit/100000 | 2759.737 | 2799.990 | -1.44% | 6.50% | 2.02% |
| BM_StrConstructorInt/100000 | 3899.491 | 3762.808 | +3.63% | 2.08% | 0.72% |
| BM_FloatConstructorFloat/100000 | 1213.275 | 1203.610 | +0.80% | 3.78% | 4.68% |
| BM_FloatConstructorInt/100000 | 2888.706 | 2796.998 | +3.28% | 1.55% | 1.82% |
| BM_FloatConstructorString/100000 | 2961.655 | 2952.272 | +0.32% | 1.17% | 0.93% |
| BM_InstanceAttributeWrite/100000 | 445.130 | 441.371 | +0.85% | 1.24% | 1.35% |
| BM_MethodKeywordCall/100000 | 822.885 | 861.479 | -4.48% | 1.14% | 1.31% |
| BM_ExpandedCallTupleDirect/100000 | 968.192 | 871.806 | +11.06% | 0.59% | 0.54% |
| BM_ExpandedCallCollectRest/100000 | 26601.519 | 26618.462 | -0.06% | 1.03% | 0.80% |
| BM_FunctionKeyword/100000 | 1187.550 | 1078.400 | +10.12% | 0.54% | 2.01% |
| BM_MemoryReclamation/100000 | 6816.081 | 6392.637 | +6.62% | 1.44% | 1.46% |

## Full-Suite A/B: Every Case

This is the single initial full-suite pass, not the interleaved medians above.

| Benchmark | Before CPU (us) | After CPU (us) | Improvement |
|---|---:|---:|---:|
| BM_RecursiveFibonacci/25 | 1447.830 | 1405.294 | +3.03% |
| BM_IterativeFibonacci/100000 | 34584.561 | 35761.897 | -3.29% |
| BM_Mandelbrot/100 | 30318.646 | 30223.913 | +0.31% |
| BM_WhileLoop/100000 | 333.830 | 343.352 | -2.77% |
| BM_ForLoop/100000 | 276.028 | 288.277 | -4.25% |
| BM_GetItemList/100000 | 640.479 | 643.907 | -0.53% |
| BM_GetItemTuple/100000 | 630.383 | 638.551 | -1.28% |
| BM_GetItemDictStringKeyed/100000 | 752.597 | 758.981 | -0.84% |
| BM_GetItemDictGeneral/100000 | 1928.071 | 2025.925 | -4.83% |
| BM_GetItemStr/100000 | 1675.502 | 1679.665 | -0.25% |
| BM_GetItemUser/100000 | 1023.086 | 1030.741 | -0.74% |
| BM_GetSliceListNonstrided/100000 | 6680.563 | 6894.740 | -3.11% |
| BM_GetSliceListGeneral/100000 | 7293.307 | 7067.412 | +3.20% |
| BM_GetSliceTupleNonstrided/100000 | 4949.441 | 5100.779 | -2.97% |
| BM_GetSliceTupleGeneral/100000 | 5297.772 | 5267.739 | +0.57% |
| BM_GetSliceStrNonstrided/100000 | 4337.887 | 4366.425 | -0.65% |
| BM_GetSliceStrGeneral/100000 | 4971.662 | 4641.738 | +7.11% |
| BM_SetItemList/100000 | 798.926 | 763.255 | +4.67% |
| BM_SetItemDictStringKeyed/100000 | 931.065 | 919.097 | +1.30% |
| BM_SetItemDictGeneral/100000 | 2375.722 | 2434.566 | -2.42% |
| BM_SetItemUser/100000 | 1235.509 | 1073.937 | +15.04% |
| BM_ForLoopSlowPath/100000 | 669.334 | 651.585 | +2.72% |
| BM_NestedForLoop/100000 | 4031.200 | 4032.807 | -0.04% |
| BM_ExceptionBareHandlerNoRaise/100000 | 543.935 | 494.893 | +9.91% |
| BM_ExceptionTypedHandlerNoRaise/100000 | 506.521 | 497.462 | +1.82% |
| BM_ExceptionBareHandlerRaise/10000 | 363.343 | 350.263 | +3.73% |
| BM_ExceptionTypedHandlerRaise/10000 | 374.608 | 365.321 | +2.54% |
| BM_ClassInstantiationNoInit/100000 | 1850.927 | 1789.294 | +3.44% |
| BM_ClassInstantiationWithInit/100000 | 2817.121 | 2799.896 | +0.62% |
| BM_StrConstructorString/100000 | 2940.052 | 2742.681 | +7.20% |
| BM_StrConstructorInt/100000 | 4483.383 | 3782.222 | +18.54% |
| BM_FloatConstructorFloat/100000 | 1192.500 | 1207.339 | -1.23% |
| BM_FloatConstructorInt/100000 | 2915.745 | 2842.972 | +2.56% |
| BM_FloatConstructorString/100000 | 2940.133 | 2990.076 | -1.67% |
| BM_IntConstructorInt/100000 | 922.301 | 894.293 | +3.13% |
| BM_IntConstructorString/100000 | 1100.464 | 1093.402 | +0.65% |
| BM_InstanceAttributeAddMember/100000 | 3115.194 | 3141.453 | -0.84% |
| BM_InstanceAttributeAddAfterConstruction/100000 | 3034.691 | 2968.761 | +2.22% |
| BM_InstanceAttributeRead/100000 | 463.416 | 438.594 | +5.66% |
| BM_ClassAttributeRead/100000 | 616.412 | 617.494 | -0.18% |
| BM_InstanceAttributeWrite/100000 | 500.394 | 434.951 | +15.05% |
| BM_ClassAttributeWrite/100000 | 928.760 | 901.449 | +3.03% |
| BM_MethodCall/100000 | 778.157 | 705.450 | +10.31% |
| BM_MethodKeywordCall/100000 | 819.787 | 863.500 | -5.06% |
| BM_FunctionDefaultParameter/100000 | 792.336 | 774.932 | +2.25% |
| BM_ExpandedCallTupleDirect/100000 | 947.922 | 848.032 | +11.78% |
| BM_ExpandedCallTuple/100000 | 5642.249 | 5599.415 | +0.76% |
| BM_ExpandedCallKeywords3Direct/100000 | 1199.271 | 1154.751 | +3.86% |
| BM_ExpandedCallKeywords3/100000 | 15469.281 | 15562.633 | -0.60% |
| BM_ExpandedCallKeywords10Direct/100000 | 2444.869 | 2351.216 | +3.98% |
| BM_ExpandedCallKeywords10/100000 | 28171.240 | 28526.265 | -1.24% |
| BM_ExpandedCallCombined/100000 | 11228.895 | 11224.613 | +0.04% |
| BM_ExpandedCallMixed/100000 | 19158.260 | 19289.808 | -0.68% |
| BM_ExpandedCallCollectBoth/100000 | 8298.185 | 8185.959 | +1.37% |
| BM_ExpandedCallCollectAll/100000 | 17567.863 | 17295.975 | +1.57% |
| BM_ExpandedCallCollectRest/100000 | 29912.980 | 26518.830 | +12.80% |
| BM_ExpandedCallMethodFunction/100000 | 8450.327 | 8295.845 | +1.86% |
| BM_ExpandedCallMethod/100000 | 8282.150 | 8036.385 | +3.06% |
| BM_FunctionKeyword/100000 | 1166.814 | 1053.474 | +10.76% |
| BM_FunctionKeywordMixed/100000 | 1037.318 | 991.641 | +4.61% |
| BM_FunctionKeywordDefault/100000 | 1048.099 | 1072.806 | -2.30% |
| BM_FunctionKwargsCapture/100000 | 9183.914 | 9035.987 | +1.64% |
| BM_FunctionKwargsLookup/100000 | 11663.567 | 10722.313 | +8.78% |
| BM_FunctionKwargsMixed/100000 | 10868.481 | 10473.481 | +3.77% |
| BM_FunctionVarargs/100000 | 2650.880 | 2622.124 | +1.10% |
| BM_FunctionVarargsWithPositional/100000 | 2392.097 | 2368.762 | +0.99% |
| BM_GlobalRead/100000 | 294.682 | 293.353 | +0.45% |
| BM_BuiltinLookup/100000 | 1840.041 | 1745.027 | +5.44% |
| BM_GlobalWrite/100000 | 377.728 | 361.275 | +4.55% |
| BM_GlobalRefcountedWrite/100000 | 592.569 | 551.567 | +7.43% |
| BM_GlobalAddDelete/100000 | 17951.590 | 17261.130 | +4.00% |
| BM_FunctionDefaultVarargs/100000 | 2181.591 | 2172.967 | +0.40% |
| BM_InstanceAttributeRefcountedWrite/100000 | 777.056 | 755.189 | +2.90% |
| BM_MethodCallClassAttributeWrite/100000 | 1339.297 | 1347.297 | -0.59% |
| BM_MemoryReclamation/100000 | 6761.354 | 6401.614 | +5.62% |
| BM_NBody/100000 | 525699.000 | 531253.000 | -1.05% |
| BM_PystoneLite/10000 | 1965.600 | 1951.303 | +0.73% |
| BM_Pystone/10000 | 4886.958 | 4835.240 | +1.07% |
| BM_PystoneArithmetic/10000 | 6785.551 | 6690.143 | +1.43% |
| BM_IntBaseFormatting/0 | 518.093 | 531.178 | -2.46% |
| BM_IntBaseFormatting/1 | 460.114 | 458.294 | +0.40% |
| BM_IntBaseFormatting/2 | 424.172 | 425.865 | -0.40% |
| BM_IntBaseFormatting/3 | 3987.716 | 3981.940 | +0.15% |
| BM_IntBaseFormatting/4 | 2379.385 | 2346.334 | +1.41% |
| BM_IntBaseFormatting/5 | 1559.126 | 1538.753 | +1.32% |
| BM_LocalEntryClears/0 | 84.212 | 85.617 | -1.64% |
| BM_LocalEntryClears/1 | 72.102 | 70.939 | +1.64% |
| BM_LocalEntryClears/2 | 96.651 | 98.761 | -2.14% |
| BM_LocalEntryClears/3 | 71.823 | 71.848 | -0.04% |
| BM_LocalEntryClears/4 | 96.624 | 99.420 | -2.81% |
| BM_LocalEntryClears/5 | 91.403 | 89.555 | +2.06% |
| BM_LocalEntryClears/6 | 72.026 | 71.665 | +0.50% |

## Layout And Code Generation

The layout probe covered all registered native types, HeapObject, and four
Instance capacities (52 rows). No allocator stride changed. HeapObject remains
eight bytes and changes alignment from four to eight. ValidityCell changes from
12 bytes/alignment four to 16 bytes/alignment eight, but retains a 32-byte stride.
Float stays 32 bytes, with its double at offset 24 and a 32-byte stride.

The standalone Header and containing HeapObject are each statically checked as
eight bytes. Normal state/native-ID/aux/RC offsets are 0/1/2/4. Release probes
show fully inlined access and memcpy folding as intended:

```asm
; generation
ldrb w8, [x0]
and  w0, w8, #0x7
ret

; RC increment
ldr  w8, [x0, #4]
add  w8, w8, #1
str  w8, [x0, #4]
mov  x0, x8
ret

; forwarding read, implemented with memcpy
ldr  x0, [x0]
ret

; forwarding write, implemented with memcpy
str  x1, [x0]
ret
```

The hot-opcode frame checker reports no new warnings. No interpreter handler or
dispatch implementation was changed.

## Profiling Limits

Five-second native samples at a 10 ms interval were collected for both binaries
on MethodKeywordCall and ExpandedCallTupleDirect, during 20-second benchmark
runs. These profiled runs are not used for the timing tables above.

Method keyword profiles have essentially the same distribution: the method
keyword handler accounts for 227/416 leaf samples before and 226/416 after.
Tuple-direct profiles remain in the interpreter call/arithmetic/load handlers.
No separate HeapHeader accessor frame appears, consistent with the Release
probes. These samples do not isolate the instruction-level cause of the timing
differences; attributing them to packing or code alignment would be inference.

## Verification And Local Artifacts

- Focused checks: 170 tests passed.
- Full Debug all/check: 2,329 passed, three existing runtime-JIT-disabled skips.
- Full Release all/check: 2,318 passed, three existing runtime-JIT-disabled skips.
- Each full suite also reports one disabled test.
- No runtime source changed between these checks and the measurements.

Raw local JSON: `/private/tmp/clover-header-baseline.json`,
`/private/tmp/clover-header-after.json`, and
`/private/tmp/clover-header-ab-{a1,b1,b2,a2}.json`.
Layout CSVs: `/private/tmp/clover-header-layout-{baseline,after}.csv`.
Profiles: `/private/tmp/clover-header-profile-{method,tuple}-{a,b}.txt`.
These paths are local measurement artifacts, not repository dependencies.

## Generation-Preserving Forwarding Revision

This is a separate, focused comparison on 2026-11-01, not a rerun of the full
92-case suite above. A is the saved Release benchmark executable from
`21303dce`; B is the same checkout with generation in bits 0--1, a forwarded
flag in bit 2, and RC lifecycle in bits 6--7. Header size, alignment, field
offsets, and benchmark inputs do not change. Both builds have runtime JIT
tiering disabled. The executable has no VM-library dependency, and these
workloads import no native extensions, so rebuilding B does not alter A.

The selected question is normal-mode RC/construction/reclamation overhead.
Production forwarding is disabled in both binaries; these measurements do not
measure forwarding or copying throughput. This focus thread bypassed the shared
build lock as authorized. Its own builds, tests, and timed runs were sequential;
CPU-time variation and ordinary shared-machine noise still apply.

Three A/B/B/A rounds give six independent CPU-time samples per binary and case:

```sh
BENCH_BINARY --benchmark_filter='^BM_(ClassInstantiationNoInit|ClassInstantiationWithInit|GlobalRefcountedWrite|InstanceAttributeRefcountedWrite|MemoryReclamation)/100000$' \
  --benchmark_min_time=0.5s --benchmark_counters_tabular=true \
  --benchmark_out=RESULT.json --benchmark_out_format=json
```

Times below are medians in milliseconds per benchmark iteration. Positive
improvement means faster: `(before / after - 1) * 100`. CV is sample standard
deviation divided by mean, in percent.

| Case, each with argument 100000 | Before ms | After ms | Improvement | CV before / after |
|---|---:|---:|---:|---:|
| ClassInstantiationNoInit | 1.7338 | 1.7936 | -3.33% | 2.28% / 3.75% |
| ClassInstantiationWithInit | 2.8092 | 2.7995 | +0.35% | 0.64% / 1.71% |
| GlobalRefcountedWrite | 0.559572 | 0.559800 | -0.04% | 0.18% / 0.30% |
| InstanceAttributeRefcountedWrite | 0.767118 | 0.767091 | +0.00% | 0.54% / 0.51% |
| MemoryReclamation | 6.3329 | 6.4237 | -1.41% | 1.50% / 1.00% |

The two slower cases were repeated in three more A/B/B/A rounds with a
two-second minimum per sample, using the same command with this narrower filter:

```text
^BM_(ClassInstantiationNoInit|MemoryReclamation)/100000$
```

| Confirmation case | Before ms | After ms | Improvement | CV before / after |
|---|---:|---:|---:|---:|
| ClassInstantiationNoInit | 1.7402 | 1.7073 | +1.93% | 3.09% / 3.91% |
| MemoryReclamation | 6.3308 | 6.5139 | -2.81% | 1.89% / 3.32% |

The construction result changes sign and does not establish a repeatable
regression. Refcounted writes are essentially unchanged in the initial series.
Reclamation is slower in both series, approximately 1--3%, with variation; this
must not be rounded into a blanket performance-neutral claim. No instruction-level
cause is established, and the forwarding mask is not exercised by these RC-mode
workloads. Separate native samples are not included in the timing statistics.

Two-second native samples show the same broad hotspots in both binaries:
reclamation, tuple construction, interpreter work, and memory initialization.
They also include benchmark-harness/reference work. These coarse samples do
not isolate an instruction-level explanation for the small reclamation change.

### Revision Code Generation and Correctness

Clang `-std=c++20 -O3 -DNDEBUG` probes against the baseline and revised headers
produce the following AArch64 sequences, omitting returns:

| Accessor | Baseline | Revision |
|---|---|---|
| Generation | `ldrb; and #7` | `ldrb; and #3` |
| Forwarded predicate | `ldrb; tst #7; cset eq` | `ldrb; ubfx #2, #1` |
| Forwarding target | `ldr` | `ldr; and #0xfffffffffffffff8` |
| Install forwarding | `str` | `ldrb; and #3; orr target; orr #4; str` |
| RC lifecycle read | `ldrb; ubfx #3, #2` | `ldrb; lsr #6` |
| RC lifecycle write | `ldrb; and; orr; strb` | `ldrb; bfi; strb` |
| Refcount read | `ldr w0, [x0, #4]` | unchanged |

The normal lifecycle store is one instruction shorter in this probe; that is
not evidence of an overall reclamation speedup. Header and HeapObject remain
eight bytes, and Float remains 32 bytes with its value at offset 24. The
interpreter-object hot-opcode frame checker emits no diagnostics before or after.

Validation passed: 153 focused header/layout/reclamation/GC tests, full Debug
`all check` with 2,342 passed, and full Release with 2,328 passed. Each full
suite has three existing JIT-disabled skips and one disabled test. New Debug
death tests reject normal-only access after forwarding, invalid generation
zero, null/unaligned targets, and repeated forwarding. These do not activate
production movement or allow RC reclamation to inspect forwarded sources.

Raw local artifacts for this revision are in
`/private/tmp/clover-generation-header.twImkM/`: initial JSON
`{before,after}-ROUND-SLOT.json`, confirmation JSON
`confirm-{before,after}-ROUND-SLOT.json`, assembly `header-{before,after}.s`,
validation logs, saved binaries, and separate `sample-{before,after}.txt`
native profiles. These are local evidence, not repository dependencies.

## NonMoving Pointer-Tag Revision

On 2026-11-01, compare source baseline `6ebce070` with the NonMoving header
protocol working tree. Header/HeapObject remain eight bytes with unchanged field
offsets; Float remains 32 bytes with its payload at offset 24. Construction
policy is not wired yet: constructors leave NonMoving clear, and no production
caller forwards objects or marks them non-moving.

Before rebuilding, preserve the Release benchmark executable as `before`.
`otool -L` shows no VM-library dependency. After rebuilding, preserve `after`.
The two complete executables are byte-for-byte identical, with SHA-256:

```text
eed05c96a3b8bc2ab54f7b00d4c7590af836d0e0b4e90e8941c48eeaa64df347
```

Thus this benchmark image has no changed executable code or data. The following
short A/B/B/A run is a timing-noise control, not evidence of a source-change
speedup or regression. Full Debug and focused Release checks finished before
timing; no owned build/test job overlapped these runs. The focus thread bypassed the
shared build lock as authorized; the lock and other jobs were untouched.

```sh
BENCH_BINARY --benchmark_filter='^BM_(ClassInstantiationNoInit|ClassInstantiationWithInit|GlobalRefcountedWrite|InstanceAttributeRefcountedWrite|MemoryReclamation)/100000$' \
  --benchmark_min_time=0.5s --benchmark_counters_tabular=true \
  --benchmark_out=RESULT.json --benchmark_out_format=json
```

One A/B/B/A round supplies two independent samples per binary and case. Values
are median CPU milliseconds per benchmark iteration; the apparent improvement
is `(before / after - 1) * 100`. Positive means faster, but these are identical
binaries and the differences below are not attributable to the header change.

| Case, each with argument 100000 | Before ms | After ms | Apparent improvement |
|---|---:|---:|---:|
| ClassInstantiationNoInit | 1.647115 | 1.703898 | -3.33% |
| ClassInstantiationWithInit | 2.779127 | 2.739200 | +1.46% |
| GlobalRefcountedWrite | 0.569045 | 0.555538 | +2.43% |
| InstanceAttributeRefcountedWrite | 0.760077 | 0.758458 | +0.21% |
| MemoryReclamation | 6.553820 | 6.443824 | +1.71% |

Standalone header probes compiled with Apple Clang 21, C++20, `-O2 -DNDEBUG`,
show the following differences (return instructions omitted):

| Operation | Before | After |
|---|---|---|
| Generation read | `ldrb; and #3` | identical |
| Refcount read | `ldr w0, [x0, #4]` | identical |
| Lifecycle write | byte load/update/store | identical |
| Forwarded Boolean | `ldrb; ubfx #2, #1` | `ldrb; tst #0x18; cset ne` |
| Forwarding target | `ldr; and ~7` | `ldr; and ~3` |
| Forwarding publication | `ldrb; and #3; orr target; orr #4; str` | `ldrb; and #3; orr target; str` |
| New NonMoving read | absent | `ldrb; ubfx #2, #1` |
| New NonMoving mark | absent | `ldrb; orr #4; strb` |

Materializing a forwarded Boolean adds one instruction; publication removes one.
These cold helper probes are not measurements of production copying throughput.
Normal generation/refcount/lifecycle probes are unchanged. The hot-opcode frame
checker emits no diagnostics. Construction initialization and eventual movement
need their own measurements when those paths become active.

Raw local artifacts are in `/private/tmp/clover-nonmoving-ab.bjK0eS/`: saved
`before`/`after` binaries, `A1.json`, `B1.json`, `B2.json`, `A2.json`, common
`header_probe.cpp` with `before.s`/`after.s`, and the separate
`nonmoving_probe.cpp`/`nonmoving.s`. These are local evidence, not repository
dependencies.
