# Loop local entry clearing: performance

Measured 2026-10-31 for GC slice 4d.2b.1. Baseline is `7db447f0` plus
the same loop benchmark extensions used after the change. After is the
uncommitted loop-summary implementation. This extends, rather than replaces,
[the normal-join measurements](gc-local-entry-clears-performance.md).

## Result

- Eight loop-local prologue clears add **1.90–1.97 ns per invocation** in repeated
  runs of the deliberately tiny stress function: **23.1–24.2% more time**.
- The one-local `while` case has no reliably resolved incremental cost: its
  medians improve slightly while the unchanged control also improves. Do not
  interpret that as a speedup from clearing.
- The `for`-target case adds **0.39–0.53 ns per invocation** in the repeated
  pairs (**4.3–5.9% more time**). This includes the surrounding range-loop work;
  it is not a universal cost per clear.
- The 92-case full sweep has median improvement **+0.30%**, geometric-mean
  improvement **+0.33%**. Individual cases swing both ways; these single sweeps
  do not establish a broad VM speedup.
- Global add/delete retains an unexplained difference: **6.0% more time** in the
  confirmation medians. Short samples do not identify a new hot path. This is
  an unresolved performance result, not a demonstrated per-clear cost or a
  discrepancy dismissed as noise.

Every new clear is in the function prologue, not a body, backedge, or exit.
The benchmark bodies for modes 3/4/5 gain one/eight/one clears respectively;
mode 6 preinitializes its local and gains none. The benchmark driver also gains
one clear for its own `i` target, once per 10,000-call batch. Modes 0–2 retain
the earlier normal-join workloads and serve as additional controls.

## Setup

Mac-mini, ARM64, 10 CPU cores; Clang Release, `CLOVERVM_ENABLE_JIT=OFF`,
Google Benchmark 1.8.5, CPython 3.15.2. Frequency discovery and affinity warnings
were present. Launch load averages were 5.36/4.52/4.55 before and 6.28/6.03/5.50
after. Affinity, executable layout, and unrelated machine activity were not
controlled. No builds or full test suites overlapped timed benchmark runs.

The baseline executable was copied before changing compiler behavior to
`/private/tmp/clovervm-loop-clears-baseline-bench`. `otool -L` confirms it links
only system libraries, not a rebuilt CloverVM dylib. Both full runs completed
all 92 cases with no benchmark errors. Benchmark Python sources and arguments
were identical on both sides. Measurements cover runtime, not compilation
throughput, precise root scanning, or moving collection.

## Commands

Full sweep before and after:

```sh
build-release/benchmark/bench_clovervm --benchmark_min_time=2s --benchmark_counters_tabular=true --benchmark_out=/private/tmp/clovervm-loop-clears-before.json --benchmark_out_format=json
build-release/benchmark/bench_clovervm --benchmark_min_time=2s --benchmark_counters_tabular=true --benchmark_out=/private/tmp/clovervm-loop-clears-after.json --benchmark_out_format=json
```

Serial repeated before/after pairs, five repetitions of each case:

```sh
/private/tmp/clovervm-loop-clears-baseline-bench --benchmark_filter='BM_LocalEntryClears/[3-6]$|BM_IntBaseFormatting/0$' --benchmark_min_time=1s --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=/private/tmp/clovervm-loop-clears-before-repeat.json --benchmark_out_format=json
build-release/benchmark/bench_clovervm --benchmark_filter='BM_LocalEntryClears/[3-6]$|BM_IntBaseFormatting/0$' --benchmark_min_time=1s --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=/private/tmp/clovervm-loop-clears-after-repeat.json --benchmark_out_format=json
/private/tmp/clovervm-loop-clears-baseline-bench --benchmark_filter='BM_LocalEntryClears/[3-6]$|BM_GlobalAddDelete/' --benchmark_min_time=1s --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=/private/tmp/clovervm-loop-clears-before-confirm.json --benchmark_out_format=json
build-release/benchmark/bench_clovervm --benchmark_filter='BM_LocalEntryClears/[3-6]$|BM_GlobalAddDelete/' --benchmark_min_time=1s --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=/private/tmp/clovervm-loop-clears-after-confirm.json --benchmark_out_format=json
```

All tables use CPU nanoseconds. Positive improvement means better:
`(before / after - 1) * 100`. The stress cases invoke the selected helper
10,000 times per benchmark iteration; divide the CPU-time difference by 10,000
for the unadjusted per-call difference.

## Repeated results

Mode 3: one conditionally executed `while`-body local. Mode 4: eight such locals.
Both break after the first executed body. Mode 5: a zero/one-iteration
`for item in range(value & 1)`. Mode 6: a local initialized before the `while`,
so it requires no new clear. The helper returns its input, not the unread locals.

Medians of the first five repetitions:

| Benchmark | Before CPU ns | After CPU ns | Improvement |
| --- | ---: | ---: | ---: |
| `BM_IntBaseFormatting/0` | 566,927.1 | 550,536.8 | +2.98% |
| `BM_LocalEntryClears/3` | 76,578.4 | 75,361.8 | +1.61% |
| `BM_LocalEntryClears/4` | 82,212.7 | 101,164.2 | -18.73% |
| `BM_LocalEntryClears/5` | 91,555.0 | 95,492.0 | -4.12% |
| `BM_LocalEntryClears/6` | 76,180.7 | 75,640.0 | +0.71% |

Confirmation medians:

| Benchmark | Before CPU ns | After CPU ns | Improvement |
| --- | ---: | ---: | ---: |
| `BM_GlobalAddDelete/100000` | 16,448,977.3 | 17,442,178.6 | -5.69% |
| `BM_LocalEntryClears/3` | 75,795.8 | 74,816.9 | +1.31% |
| `BM_LocalEntryClears/4` | 81,345.3 | 101,046.7 | -19.50% |
| `BM_LocalEntryClears/5` | 89,940.1 | 95,230.2 | -5.56% |
| `BM_LocalEntryClears/6` | 76,208.6 | 73,827.8 | +3.22% |

Some controls have CPU coefficients of variation of a few percent (up to 4.8%
for the first baseline single-local case). Tiny single-clear differences cannot
be separated reliably from that drift. The eight-clear difference is much
larger and repeats across both pairs.

The full-sweep binary-formatting delta does not hold up: repeated medians move
from 566,927 ns before to 550,537 ns after, reversing the initial regression.
Global add/delete does repeat. That function gains one `ClearLocal r1` at entry
for its `for` target, not an instruction in the 100,000-iteration loop. Matching
3-second macOS samples at 10-ms intervals show the same shape add/delete
transitions, transition-vector allocation, overflow stores, and reclamation
paths on both sides; no clear-local hot path appears. These coarse samples do
not prove the cause of the residual difference. A controlled follow-up is needed
before attributing it to initialization, layout, or host effects.

Diagnostic commands (these sampled runs are not extra clean timing trials):

```sh
/private/tmp/clovervm-loop-clears-baseline-bench --benchmark_filter='BM_GlobalAddDelete/100000$' --benchmark_min_time=10s
build-release/benchmark/bench_clovervm --benchmark_filter='BM_GlobalAddDelete/100000$' --benchmark_min_time=10s
sample PID 3 10 -file /private/tmp/clovervm-loop-clears-before.sample.txt
sample PID 3 10 -file /private/tmp/clovervm-loop-clears-after.sample.txt
```

## Full suite

One full sweep per side; all 92 cases are shown. Small deltas are not
statistically resolved by this table.

| Benchmark | Before CPU ns | After CPU ns | Improvement |
| --- | ---: | ---: | ---: |
| `BM_RecursiveFibonacci/25` | 1,522,836.6 | 1,539,782.9 | -1.10% |
| `BM_IterativeFibonacci/100000` | 37,261,786.7 | 36,983,948.1 | +0.75% |
| `BM_Mandelbrot/100` | 31,053,064.1 | 29,012,852.6 | +7.03% |
| `BM_WhileLoop/100000` | 358,359.1 | 347,299.5 | +3.18% |
| `BM_ForLoop/100000` | 301,626.7 | 291,359.2 | +3.52% |
| `BM_GetItemList/100000` | 674,642.0 | 647,626.2 | +4.17% |
| `BM_GetItemTuple/100000` | 678,611.9 | 649,035.2 | +4.56% |
| `BM_GetItemDictStringKeyed/100000` | 773,901.8 | 724,398.5 | +6.83% |
| `BM_GetItemDictGeneral/100000` | 2,110,666.7 | 2,053,564.9 | +2.78% |
| `BM_GetItemStr/100000` | 1,865,193.3 | 1,751,331.0 | +6.50% |
| `BM_GetItemUser/100000` | 1,088,282.3 | 1,039,464.2 | +4.70% |
| `BM_GetSliceListNonstrided/100000` | 6,978,313.6 | 6,893,620.0 | +1.23% |
| `BM_GetSliceListGeneral/100000` | 7,306,606.7 | 7,122,663.3 | +2.58% |
| `BM_GetSliceTupleNonstrided/100000` | 5,240,711.2 | 5,020,483.9 | +4.39% |
| `BM_GetSliceTupleGeneral/100000` | 5,579,641.1 | 5,348,231.6 | +4.33% |
| `BM_GetSliceStrNonstrided/100000` | 4,488,631.4 | 4,376,855.8 | +2.55% |
| `BM_GetSliceStrGeneral/100000` | 4,798,209.9 | 4,631,691.2 | +3.60% |
| `BM_SetItemList/100000` | 823,770.5 | 757,364.6 | +8.77% |
| `BM_SetItemDictStringKeyed/100000` | 968,423.5 | 948,632.6 | +2.09% |
| `BM_SetItemDictGeneral/100000` | 2,424,916.0 | 2,338,759.9 | +3.68% |
| `BM_SetItemUser/100000` | 1,142,045.6 | 1,095,476.1 | +4.25% |
| `BM_ForLoopSlowPath/100000` | 692,351.9 | 682,843.7 | +1.39% |
| `BM_NestedForLoop/100000` | 4,031,287.1 | 4,080,338.7 | -1.20% |
| `BM_ExceptionBareHandlerNoRaise/100000` | 509,059.7 | 502,537.7 | +1.30% |
| `BM_ExceptionTypedHandlerNoRaise/100000` | 514,579.8 | 510,089.4 | +0.88% |
| `BM_ExceptionBareHandlerRaise/10000` | 358,356.7 | 361,067.6 | -0.75% |
| `BM_ExceptionTypedHandlerRaise/10000` | 371,685.3 | 376,312.9 | -1.23% |
| `BM_ClassInstantiationNoInit/100000` | 1,845,587.8 | 1,795,121.3 | +2.81% |
| `BM_ClassInstantiationWithInit/100000` | 2,695,040.4 | 2,741,842.8 | -1.71% |
| `BM_StrConstructorString/100000` | 2,952,128.5 | 2,983,842.6 | -1.06% |
| `BM_StrConstructorInt/100000` | 3,942,187.9 | 3,883,153.8 | +1.52% |
| `BM_FloatConstructorFloat/100000` | 1,218,716.5 | 1,218,209.9 | +0.04% |
| `BM_FloatConstructorInt/100000` | 2,766,951.0 | 2,757,821.3 | +0.33% |
| `BM_FloatConstructorString/100000` | 2,933,192.1 | 2,891,240.5 | +1.45% |
| `BM_IntConstructorInt/100000` | 988,713.5 | 941,612.6 | +5.00% |
| `BM_IntConstructorString/100000` | 1,168,348.2 | 1,144,627.9 | +2.07% |
| `BM_InstanceAttributeAddMember/100000` | 3,096,076.0 | 3,160,499.4 | -2.04% |
| `BM_InstanceAttributeAddAfterConstruction/100000` | 2,995,938.0 | 2,973,754.5 | +0.75% |
| `BM_InstanceAttributeRead/100000` | 455,776.9 | 460,011.6 | -0.92% |
| `BM_ClassAttributeRead/100000` | 623,572.6 | 621,841.5 | +0.28% |
| `BM_InstanceAttributeWrite/100000` | 448,657.4 | 449,376.2 | -0.16% |
| `BM_ClassAttributeWrite/100000` | 913,682.4 | 914,896.7 | -0.13% |
| `BM_MethodCall/100000` | 722,123.8 | 734,537.0 | -1.69% |
| `BM_MethodKeywordCall/100000` | 853,114.0 | 853,842.8 | -0.09% |
| `BM_FunctionDefaultParameter/100000` | 776,091.0 | 778,960.1 | -0.37% |
| `BM_ExpandedCallTupleDirect/100000` | 941,231.0 | 947,437.8 | -0.66% |
| `BM_ExpandedCallTuple/100000` | 5,312,197.0 | 5,343,207.5 | -0.58% |
| `BM_ExpandedCallKeywords3Direct/100000` | 1,227,061.5 | 1,197,815.3 | +2.44% |
| `BM_ExpandedCallKeywords3/100000` | 15,324,245.9 | 15,119,724.3 | +1.35% |
| `BM_ExpandedCallKeywords10Direct/100000` | 2,444,132.9 | 2,370,349.7 | +3.11% |
| `BM_ExpandedCallKeywords10/100000` | 27,985,330.0 | 28,498,275.5 | -1.80% |
| `BM_ExpandedCallCombined/100000` | 11,049,627.0 | 10,659,780.3 | +3.66% |
| `BM_ExpandedCallMixed/100000` | 18,959,386.2 | 18,772,429.5 | +1.00% |
| `BM_ExpandedCallCollectBoth/100000` | 8,137,376.8 | 7,893,204.2 | +3.09% |
| `BM_ExpandedCallCollectAll/100000` | 17,189,109.8 | 16,752,628.7 | +2.61% |
| `BM_ExpandedCallCollectRest/100000` | 26,730,273.6 | 26,162,675.9 | +2.17% |
| `BM_ExpandedCallMethodFunction/100000` | 8,154,226.1 | 7,900,026.1 | +3.22% |
| `BM_ExpandedCallMethod/100000` | 7,780,326.0 | 7,767,848.3 | +0.16% |
| `BM_FunctionKeyword/100000` | 1,197,125.4 | 1,198,084.8 | -0.08% |
| `BM_FunctionKeywordMixed/100000` | 1,051,144.5 | 1,019,206.1 | +3.13% |
| `BM_FunctionKeywordDefault/100000` | 1,059,308.2 | 1,058,200.1 | +0.10% |
| `BM_FunctionKwargsCapture/100000` | 9,250,420.5 | 9,019,926.0 | +2.56% |
| `BM_FunctionKwargsLookup/100000` | 10,889,622.6 | 10,680,180.1 | +1.96% |
| `BM_FunctionKwargsMixed/100000` | 9,877,738.4 | 10,012,501.8 | -1.35% |
| `BM_FunctionVarargs/100000` | 2,550,911.4 | 2,527,367.8 | +0.93% |
| `BM_FunctionVarargsWithPositional/100000` | 2,233,191.0 | 2,274,850.3 | -1.83% |
| `BM_GlobalRead/100000` | 299,469.9 | 300,575.9 | -0.37% |
| `BM_BuiltinLookup/100000` | 1,883,414.4 | 1,829,433.9 | +2.95% |
| `BM_GlobalWrite/100000` | 372,714.8 | 372,814.6 | -0.03% |
| `BM_GlobalRefcountedWrite/100000` | 561,790.7 | 587,918.2 | -4.44% |
| `BM_GlobalAddDelete/100000` | 16,373,816.0 | 17,556,387.5 | -6.74% |
| `BM_FunctionDefaultVarargs/100000` | 2,089,608.2 | 2,125,665.2 | -1.70% |
| `BM_InstanceAttributeRefcountedWrite/100000` | 761,841.8 | 787,839.7 | -3.30% |
| `BM_MethodCallClassAttributeWrite/100000` | 1,342,497.2 | 1,366,988.7 | -1.79% |
| `BM_MemoryReclamation/100000` | 6,524,786.5 | 6,678,813.4 | -2.31% |
| `BM_NBody/100000` | 548,969,200.0 | 564,898,600.0 | -2.82% |
| `BM_PystoneLite/10000` | 1,991,992.3 | 2,022,542.4 | -1.51% |
| `BM_Pystone/10000` | 5,048,695.0 | 4,994,593.3 | +1.08% |
| `BM_PystoneArithmetic/10000` | 6,705,627.4 | 6,738,169.1 | -0.48% |
| `BM_IntBaseFormatting/0` | 524,943.9 | 568,374.6 | -7.64% |
| `BM_IntBaseFormatting/1` | 461,325.5 | 485,676.0 | -5.01% |
| `BM_IntBaseFormatting/2` | 419,161.0 | 443,020.4 | -5.39% |
| `BM_IntBaseFormatting/3` | 4,047,329.0 | 4,059,484.8 | -0.30% |
| `BM_IntBaseFormatting/4` | 2,388,428.8 | 2,376,318.6 | +0.51% |
| `BM_IntBaseFormatting/5` | 1,547,039.3 | 1,593,332.8 | -2.91% |
| `BM_LocalEntryClears/0` | 86,042.5 | 86,303.0 | -0.30% |
| `BM_LocalEntryClears/1` | 74,749.4 | 75,694.9 | -1.25% |
| `BM_LocalEntryClears/2` | 99,550.5 | 102,419.8 | -2.80% |
| `BM_LocalEntryClears/3` | 73,884.3 | 75,426.2 | -2.04% |
| `BM_LocalEntryClears/4` | 81,306.3 | 101,057.9 | -19.54% |
| `BM_LocalEntryClears/5` | 88,796.4 | 94,970.6 | -6.50% |
| `BM_LocalEntryClears/6` | 75,221.9 | 75,601.7 | -0.50% |

## Correctness and boundaries

- `ninja -C build-debug all check`: 2,204 passed, one existing skip and one
  disabled test.
- `ninja -C build-release all check check_opcode_frames`: 2,195 passed, the same
  skip/disabled cases; opcode-frame check passed.
- New structural and interpreter cases cover zero/multiple iterations,
  break/continue, nested loops, loop `else`, repeated-condition walrus writes,
  deletion, preserved parameters, definitely initialized controls, unreachable
  loops, and preservation of unbound checks. Structural tests require the clears
  to be contiguous at function entry and reject extra clears in the body/exits.
- The interpreter regression source also passes CPython 3.15.2. The one old
  codegen offset expectation changed by two bytes for its new target clear; the
  dominating accumulator remains uncleared and uses the existing unchecked load.
- Exceptional-flow summaries, temporary root selection, precise frame scanning,
  new safepoints, and moving collection remain out of scope.

