# Selective local entry clearing: performance

Measured 2026-10-31 for GC slice 4d.2a. Baseline is `9271f055` plus the
identical new benchmark harness; after is the uncommitted selective-normal-join
clearing implementation. The baseline executable was retained before rebuilding
and links the VM objects into the executable, not a mutable VM dylib.

## Result

Eight selectively cleared locals add approximately **2 ns per invocation** in
the deliberately tiny stress function: repeated median time increases by
25.5–25.6%. One clear adds 0.11–0.15 ns per invocation in the same measurements,
but the unchanged control also drifts by 0.09–0.14 ns; the incremental one-clear
cost is not resolved reliably. Do not extrapolate the stress-function percentage
to general programs.

The 88-case full sweep has median improvement **−0.51%** and geometric-mean
improvement **−0.82%**. Individual unchanged workloads move in both directions;
this is not evidence for a broad VM speedup or a precise whole-VM regression.
A residual difference remains in list assignment (see below); it is not
explained away as noise. The octal-formatting delta is not stable across runs.

The codegen structural checks show zero new clears for the definitely-written
control, exactly one for the one-local case, and exactly eight for the eight-local
case. Clears execute once per function invocation, not per assignment or loop
iteration. No runtime initialization bitmap or bytecode repair is involved.

## Setup and commands

Mac-mini, ARM64, macOS 26.6.2, 10 CPU cores; Clang Release build,
`CLOVERVM_ENABLE_JIT=OFF`, Google Benchmark 1.8.5, CPython 3.15.2.
CPU frequency discovery and affinity warnings were present. Full-sweep load
averages at launch were 3.21/2.98/2.65 before and 4.48/4.18/3.54 after. No builds
or tests were run concurrently with timed runs in this chat; unrelated machine
activity was not controlled. Each side passed result checks, with no benchmark
errors. This measures runtime, not compilation throughput or future root scanning.

Both full-suite runs used the same arguments (changing only the output path):

```sh
build-release/benchmark/bench_clovervm --benchmark_min_time=2s --benchmark_counters_tabular=true --benchmark_out=/private/tmp/clovervm-entry-clears-before.json --benchmark_out_format=json
build-release/benchmark/bench_clovervm --benchmark_min_time=2s --benchmark_counters_tabular=true --benchmark_out=/private/tmp/clovervm-entry-clears-after.json --benchmark_out_format=json
```

The first baseline binary was copied to
`/private/tmp/clovervm-entry-clears-baseline-bench` before building the changed
code. Repeats ran serially before/after, then before/after again:

```sh
/private/tmp/clovervm-entry-clears-baseline-bench --benchmark_filter=BM_LocalEntryClears --benchmark_min_time=1s --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=/private/tmp/clovervm-entry-clears-before-repeat.json --benchmark_out_format=json
build-release/benchmark/bench_clovervm --benchmark_filter=BM_LocalEntryClears --benchmark_min_time=1s --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=/private/tmp/clovervm-entry-clears-after-repeat.json --benchmark_out_format=json
/private/tmp/clovervm-entry-clears-baseline-bench --benchmark_filter='BM_LocalEntryClears|BM_SetItemList/|BM_FloatConstructorFloat/|BM_IntBaseFormatting/1$' --benchmark_min_time=1s --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=/private/tmp/clovervm-entry-clears-before-confirm.json --benchmark_out_format=json
build-release/benchmark/bench_clovervm --benchmark_filter='BM_LocalEntryClears|BM_SetItemList/|BM_FloatConstructorFloat/|BM_IntBaseFormatting/1$' --benchmark_min_time=1s --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=/private/tmp/clovervm-entry-clears-after-confirm.json --benchmark_out_format=json
```

All tables use CPU time in nanoseconds. **Positive improvement is better**, computed
as `(before / after - 1) * 100`. A 25.6% time increase is about a −20.4%
improvement under this convention.

## Repeated targeted results

Medians of five repetitions. Each local-entry benchmark invokes its selected
function 10,000 times. Mode 0 writes all eight locals definitely (no entry clears),
mode 1 conditionally writes one local, and mode 2 conditionally writes eight.
The helper bodies perform no heap allocation and never read those locals.

First pair:

| Benchmark | Before CPU ns | After CPU ns | Improvement |
| --- | ---: | ---: | ---: |
| `BM_LocalEntryClears/0` | 86,880.5 | 88,284.3 | -1.59% |
| `BM_LocalEntryClears/1` | 72,758.3 | 73,832.3 | -1.45% |
| `BM_LocalEntryClears/2` | 78,964.6 | 99,149.6 | -20.36% |

Confirmation pair:

| Benchmark | Before CPU ns | After CPU ns | Improvement |
| --- | ---: | ---: | ---: |
| `BM_SetItemList/100000` | 764,222.9 | 794,622.3 | -3.83% |
| `BM_FloatConstructorFloat/100000` | 1,211,575.9 | 1,213,983.6 | -0.20% |
| `BM_IntBaseFormatting/1` | 461,249.3 | 484,592.9 | -4.82% |
| `BM_LocalEntryClears/0` | 85,375.2 | 86,309.0 | -1.08% |
| `BM_LocalEntryClears/1` | 71,596.7 | 73,113.7 | -2.07% |
| `BM_LocalEntryClears/2` | 77,940.4 | 97,776.7 | -20.29% |

The float-constructor delta largely disappears on repetition. List assignment
and octal formatting retain approximately −3.8% and −4.8% differences in the
confirmation pair, despite no entry clears in either workload's emitted
bytecode. Subsequent 10-second-minimum runs used the filter
`BM_SetItemList/100000$|BM_IntBaseFormatting/1$`, after then before, with
`sample PID 3 10 -file PATH` captures during execution. Their octal times were
463,005 ns after versus 463,986 ns before: the earlier octal delta is not stable.
List assignment remained different (812,566 ns after versus 788,104 ns before).
These auxiliary sampled runs are diagnostic, not clean additional timing trials.
Short macOS `sample` captures for list assignment show the same
interpreter dispatch and trusted-list-setitem hot paths before/after, not a new
clearing path. The after octal sample is in integer formatting, length checks,
and reclamation. These coarse samples do not establish the cause of a few-percent
difference; machine scheduling and executable layout remain uncontrolled, not
proven explanations. Resolving the residual list-assignment delta needs a more controlled
follow-up; do not attribute them to selective clears without evidence.

## Full suite

Single full sweep per side; small individual deltas are not statistically
resolved by this table. All 88 cases are included.

| Benchmark | Before CPU ns | After CPU ns | Improvement |
| --- | ---: | ---: | ---: |
| `BM_RecursiveFibonacci/25` | 1,441,348.4 | 1,437,481.0 | +0.27% |
| `BM_IterativeFibonacci/100000` | 35,973,500.0 | 35,661,051.9 | +0.88% |
| `BM_Mandelbrot/100` | 28,956,877.6 | 29,736,914.0 | -2.62% |
| `BM_WhileLoop/100000` | 348,254.0 | 347,523.0 | +0.21% |
| `BM_ForLoop/100000` | 293,400.8 | 291,140.0 | +0.78% |
| `BM_GetItemList/100000` | 648,905.5 | 646,729.1 | +0.34% |
| `BM_GetItemTuple/100000` | 640,909.5 | 644,571.5 | -0.57% |
| `BM_GetItemDictStringKeyed/100000` | 729,738.0 | 753,199.5 | -3.11% |
| `BM_GetItemDictGeneral/100000` | 1,968,792.5 | 2,019,273.4 | -2.50% |
| `BM_GetItemStr/100000` | 1,690,990.9 | 1,703,124.5 | -0.71% |
| `BM_GetItemUser/100000` | 1,034,674.9 | 1,045,237.0 | -1.01% |
| `BM_GetSliceListNonstrided/100000` | 6,710,810.1 | 6,875,936.7 | -2.40% |
| `BM_GetSliceListGeneral/100000` | 6,987,795.0 | 7,038,542.7 | -0.72% |
| `BM_GetSliceTupleNonstrided/100000` | 4,932,134.5 | 4,974,790.4 | -0.86% |
| `BM_GetSliceTupleGeneral/100000` | 5,146,302.6 | 5,253,841.4 | -2.05% |
| `BM_GetSliceStrNonstrided/100000` | 4,345,078.2 | 4,409,746.1 | -1.47% |
| `BM_GetSliceStrGeneral/100000` | 4,531,481.4 | 4,602,794.0 | -1.55% |
| `BM_SetItemList/100000` | 717,070.1 | 787,088.7 | -8.90% |
| `BM_SetItemDictStringKeyed/100000` | 936,736.6 | 943,785.5 | -0.75% |
| `BM_SetItemDictGeneral/100000` | 2,376,670.7 | 2,373,594.2 | +0.13% |
| `BM_SetItemUser/100000` | 1,086,540.3 | 1,111,914.4 | -2.28% |
| `BM_ForLoopSlowPath/100000` | 707,656.6 | 695,795.2 | +1.70% |
| `BM_NestedForLoop/100000` | 3,962,422.9 | 4,011,719.9 | -1.23% |
| `BM_ExceptionBareHandlerNoRaise/100000` | 503,086.0 | 503,953.4 | -0.17% |
| `BM_ExceptionTypedHandlerNoRaise/100000` | 507,059.2 | 510,159.2 | -0.61% |
| `BM_ExceptionBareHandlerRaise/10000` | 341,017.2 | 354,216.6 | -3.73% |
| `BM_ExceptionTypedHandlerRaise/10000` | 360,162.4 | 372,824.5 | -3.40% |
| `BM_ClassInstantiationNoInit/100000` | 1,712,817.4 | 1,789,672.4 | -4.29% |
| `BM_ClassInstantiationWithInit/100000` | 2,709,581.9 | 2,698,561.4 | +0.41% |
| `BM_StrConstructorString/100000` | 3,002,625.7 | 2,931,198.7 | +2.44% |
| `BM_StrConstructorInt/100000` | 3,883,582.5 | 3,910,787.4 | -0.70% |
| `BM_FloatConstructorFloat/100000` | 1,233,091.2 | 1,339,778.4 | -7.96% |
| `BM_FloatConstructorInt/100000` | 2,761,462.1 | 2,758,957.8 | +0.09% |
| `BM_FloatConstructorString/100000` | 2,891,864.4 | 2,888,512.5 | +0.12% |
| `BM_IntConstructorInt/100000` | 972,512.6 | 970,926.7 | +0.16% |
| `BM_IntConstructorString/100000` | 1,149,386.7 | 1,181,547.2 | -2.72% |
| `BM_InstanceAttributeAddMember/100000` | 3,076,224.8 | 3,094,558.4 | -0.59% |
| `BM_InstanceAttributeAddAfterConstruction/100000` | 2,896,920.6 | 2,976,192.1 | -2.66% |
| `BM_InstanceAttributeRead/100000` | 461,390.7 | 462,594.6 | -0.26% |
| `BM_ClassAttributeRead/100000` | 626,829.4 | 620,310.4 | +1.05% |
| `BM_InstanceAttributeWrite/100000` | 443,972.8 | 441,937.3 | +0.46% |
| `BM_ClassAttributeWrite/100000` | 931,750.8 | 896,243.0 | +3.96% |
| `BM_MethodCall/100000` | 727,371.6 | 716,396.3 | +1.53% |
| `BM_MethodKeywordCall/100000` | 836,095.0 | 877,495.0 | -4.72% |
| `BM_FunctionDefaultParameter/100000` | 789,295.9 | 751,034.3 | +5.09% |
| `BM_ExpandedCallTupleDirect/100000` | 934,573.4 | 920,334.8 | +1.55% |
| `BM_ExpandedCallTuple/100000` | 5,334,528.5 | 5,356,059.3 | -0.40% |
| `BM_ExpandedCallKeywords3Direct/100000` | 1,183,167.6 | 1,134,511.9 | +4.29% |
| `BM_ExpandedCallKeywords3/100000` | 15,598,910.6 | 15,622,050.3 | -0.15% |
| `BM_ExpandedCallKeywords10Direct/100000` | 2,527,187.2 | 2,319,681.5 | +8.95% |
| `BM_ExpandedCallKeywords10/100000` | 28,387,585.9 | 28,722,793.8 | -1.17% |
| `BM_ExpandedCallCombined/100000` | 10,951,696.5 | 10,935,144.5 | +0.15% |
| `BM_ExpandedCallMixed/100000` | 19,122,883.6 | 19,517,645.8 | -2.02% |
| `BM_ExpandedCallCollectBoth/100000` | 8,052,813.8 | 8,116,927.5 | -0.79% |
| `BM_ExpandedCallCollectAll/100000` | 17,377,018.4 | 17,077,926.8 | +1.75% |
| `BM_ExpandedCallCollectRest/100000` | 26,156,560.7 | 26,253,429.9 | -0.37% |
| `BM_ExpandedCallMethodFunction/100000` | 8,152,542.0 | 8,135,328.4 | +0.21% |
| `BM_ExpandedCallMethod/100000` | 7,786,944.3 | 7,718,193.5 | +0.89% |
| `BM_FunctionKeyword/100000` | 1,227,546.2 | 1,190,726.6 | +3.09% |
| `BM_FunctionKeywordMixed/100000` | 1,054,139.5 | 987,959.6 | +6.70% |
| `BM_FunctionKeywordDefault/100000` | 1,075,624.9 | 1,081,711.7 | -0.56% |
| `BM_FunctionKwargsCapture/100000` | 9,313,860.9 | 9,183,611.8 | +1.42% |
| `BM_FunctionKwargsLookup/100000` | 11,037,913.7 | 10,826,267.4 | +1.95% |
| `BM_FunctionKwargsMixed/100000` | 9,761,194.4 | 9,985,176.3 | -2.24% |
| `BM_FunctionVarargs/100000` | 2,570,306.5 | 2,579,225.8 | -0.35% |
| `BM_FunctionVarargsWithPositional/100000` | 2,289,202.3 | 2,294,383.6 | -0.23% |
| `BM_GlobalRead/100000` | 309,387.0 | 297,732.4 | +3.91% |
| `BM_BuiltinLookup/100000` | 1,884,974.4 | 1,861,052.5 | +1.29% |
| `BM_GlobalWrite/100000` | 386,112.0 | 364,147.8 | +6.03% |
| `BM_GlobalRefcountedWrite/100000` | 604,531.9 | 563,533.9 | +7.28% |
| `BM_GlobalAddDelete/100000` | 18,471,021.0 | 17,433,111.1 | +5.95% |
| `BM_FunctionDefaultVarargs/100000` | 2,115,524.8 | 2,181,882.1 | -3.04% |
| `BM_InstanceAttributeRefcountedWrite/100000` | 783,171.1 | 777,160.8 | +0.77% |
| `BM_MethodCallClassAttributeWrite/100000` | 1,377,747.7 | 1,368,881.1 | +0.65% |
| `BM_MemoryReclamation/100000` | 6,727,482.0 | 7,035,368.7 | -4.38% |
| `BM_NBody/100000` | 531,655,800.0 | 545,475,400.0 | -2.53% |
| `BM_PystoneLite/10000` | 2,025,278.5 | 2,034,492.3 | -0.45% |
| `BM_Pystone/10000` | 5,020,698.0 | 5,110,348.5 | -1.75% |
| `BM_PystoneArithmetic/10000` | 6,864,556.4 | 6,889,639.5 | -0.36% |
| `BM_IntBaseFormatting/0` | 528,090.3 | 561,795.3 | -6.00% |
| `BM_IntBaseFormatting/1` | 462,892.4 | 499,701.7 | -7.37% |
| `BM_IntBaseFormatting/2` | 426,270.1 | 450,139.6 | -5.30% |
| `BM_IntBaseFormatting/3` | 4,045,206.1 | 4,139,002.9 | -2.27% |
| `BM_IntBaseFormatting/4` | 2,325,226.3 | 2,424,517.9 | -4.10% |
| `BM_IntBaseFormatting/5` | 1,554,844.9 | 1,605,146.3 | -3.13% |
| `BM_LocalEntryClears/0` | 85,550.6 | 86,588.9 | -1.20% |
| `BM_LocalEntryClears/1` | 70,712.3 | 73,975.3 | -4.41% |
| `BM_LocalEntryClears/2` | 76,533.7 | 98,768.5 | -22.51% |

## Correctness and scope

- Focused codegen and interpreter checks: 140 passed.
- Final `ninja -C build-debug all check`: 2,197 passed, one skipped,
  one disabled.
- Final `ninja -C build-release all check check_opcode_frames`: 2,188 passed,
  one skipped, one disabled; opcode-frame check passed.
- Normal joins only: if/elif/else, conditional expressions, short-circuit
  boolean expressions, and comparison chains. Source-level unbound checks remain
  separate; clearing storage does not make a local semantically bound.
- Loop/exception summaries, temporary root selection, and precise frame-root
  scanning remain deferred. This slice does not enable moving collection.

