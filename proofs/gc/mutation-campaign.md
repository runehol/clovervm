# Mutation campaign

Tested code: `a73d455d525bf38b382e05873c5e243969294e53` (2026-11-09).

All 30 Lean mutants compiled as implementations and then failed actual theorem proofs. Tests, examples, axiom audits and lint failures were not counted as proof detection. All 30 C++ counterparts compiled and were caught by directed GC tests; 27 also differed on the eight valid-heap comparisons. The missing weak-coverage mutant additionally accepted the malformed-input probe.

Each mutation was applied alone and restored before the next experiment. The comparisons used a frozen pristine opposite-side runner, driven by standard CPython. No mutants were committed.

| # | Mutation | Lean proof that caught it | C++ detection |
|---|---|---|---|
| 01 | Move old objects in young collection | [remembered_unselected](CloverGC/GenerationalProofs.lean#L13) | GC tests; differential (3/8) |
| 02 | Drop unreferenced old allocations (capture boundary) | [captureStrong_identities](CloverGC/Differential/HeapRunnerProofs.lean#L322) | GC tests; differential (4/8) |
| 03 | Skip remembered owners | [coverage](CloverGC/GenerationalProofs.lean#L28) | GC tests; differential (3/8) |
| 04 | Skip remembered-owner weak fields | [rememberOwner_registered](CloverGC/GenerationalWeakScheduling.lean#L28) | GC tests; differential (1/8) |
| 05 | Skip remembered-owner strong fields | [rememberOwners_strong](CloverGC/GenerationalWeakDriverProofs.lean#L25) | GC tests; differential (2/8) |
| 06 | Discover movable weak targets strongly | [earlyVisits_strong](CloverGC/GenerationalWeakDriverProofs.lean#L11) | GC tests; differential (2/8) |
| 07 | Ignore weak entry payload repair | [early_complete_correct](CloverGC/WeakResolutionProofs.lean#L199) | GC tests; differential (2/8) |
| 08 | Early Pending becomes Dead | [resolve_early_not_dead](CloverGC/WeakResolutionProofs.lean#L14) | GC tests; differential (1/8) |
| 09 | Skip deferred repair | [collection_late_exact](CloverGC/GenerationalWeakScheduling.lean#L144) | GC tests; differential (3/8) |
| 10 | Late unresolved targets remain Pending | [resolve_late_not_pending](CloverGC/WeakResolutionProofs.lean#L24) | GC tests; differential (2/8) |
| 11 | Forward to the first copy instead of the right copy | [install_ledger](CloverGC/CopyDiscoveryProofs.lean#L104) | GC tests; differential (2/8) |
| 12 | Reset only part of a dead entry | [repairUnit_target](CloverGC/WeakResolutionProofs.lean#L314) | GC tests; differential (2/8) |
| 13 | Register owner weak units twice | [rememberOwner_registered](CloverGC/GenerationalWeakScheduling.lean#L28) | GC tests |
| 14 | Accept missing weak remembered coverage (wire validator) | [validateHeap_weak_covered](CloverGC/Differential/HeapFormatProofs.lean#L40) | GC tests; invalid-input validation |
| 15 | Turn immediate zero into null weak cell | [early_cell_compatible](CloverGC/WeakResolutionProofs.lean#L118) | GC tests; differential (2/8) |
| 16 | Omit the first strong root | [full_evacuate](CloverGC/GenerationalProofs.lean#L136) | GC tests; differential (5/8) |
| 17 | Skip reachable-object strong children | [full_scan](CloverGC/GenerationalProofs.lean#L123) | GC tests; differential (3/8) |
| 18 | Allocate again for an already-forwarded object | [discovery_preserves](CloverGC/CopyDiscoveryProofs.lean#L193) | GC tests; differential (4/8) |
| 19 | Reverse repaired strong fields while retaining reachability | [write_get](CloverGC/EvacuationFields.lean#L116) | GC tests; differential (5/8) |
| 20 | Treat unselected old weak targets as unresolved/dead | [resolve_unselected](CloverGC/GenerationalWeakProofs.lean#L11) | GC tests; differential (1/8) |
| 21 | Resolve unmarked nonmoving targets as live | [resolve_early_not_dead](CloverGC/WeakResolutionProofs.lean#L14) | GC tests |
| 22 | Reset live weak entries | [early_unit_compatible](CloverGC/WeakResolutionProofs.lean#L171) | GC tests; differential (1/8) |
| 23 | Keep a dead payload when both guards live | [repair_late_complete](CloverGC/WeakResolutionProofs.lean#L155) | GC tests; differential (2/8) |
| 24 | Use the wrong copy destination kind | [install_ledger](CloverGC/CopyDiscoveryProofs.lean#L124) | GC tests; differential (2/8) |
| 25 | Change copied logical identity during capture | [captureCopy_identity](CloverGC/Differential/HeapRunnerProofs.lean#L302) | GC tests; differential (6/8) |
| 26 | Change destination allocation ordinal during capture | [captureCopy_identity](CloverGC/Differential/HeapRunnerProofs.lean#L302) | GC tests; differential (6/8) |
| 27 | Drop a root during snapshot projection | [captureStrong_roots](CloverGC/Differential/HeapRunnerProofs.lean#L482) | GC tests; differential (7/8) |
| 28 | Reverse strong fields during snapshot projection | [captureOriginal_strong](CloverGC/Differential/HeapRunnerProofs.lean#L452) | GC tests; differential (5/8) |
| 29 | Mistag an immediate as null during weak projection | [captureCell_correct](CloverGC/Differential/HeapRunnerProofs.lean#L238) | GC tests; differential (7/8) |
| 30 | Reject valid nonempty heaps | [collectPrepared_mark_complete](CloverGC/Differential/HeapMarkCompleteness.lean#L46) | GC tests; differential (8/8) |

The table names one failing theorem per Lean mutation; several mutations failed additional proofs. Valid-heap differential comparison alone missed duplicate weak registration, missing weak coverage and the unmarked-nonmoving-target mutation. Directed tests caught all three; the malformed-input probe also caught missing weak coverage.

The proof results cover the Lean model and runner contracts. They are not a formal refinement proof of the production C++ collector.

After restoring all sources, the pristine Debug suite passed (2,409 tests; three skipped and one disabled), as did the complete Lean build and axiom audits, 131 format checks, 605 execution checks, 41 CLI checks and 22 standard-CPython tests. A fresh generated campaign passed 100 full-mark, 100 full-evacuation and 100 young-evacuation comparisons (seed 132, at most 20 objects).
