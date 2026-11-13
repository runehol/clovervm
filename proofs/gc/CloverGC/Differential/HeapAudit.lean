import CloverGC.Differential.HeapCollectionCompleteness

/-!
Axiom checks for the heap adapter's public boundaries. `#print axioms` follows
proof dependencies transitively, so supporting lemmas need no separate audit.
The additional independent claims below retain coverage outside those boundaries.
Collector algorithms keep their own core audits.
-/

/- Validation, preparation and whole-run guarantees. -/
/-- info: 'CloverGC.Differential.Format.prepareFreshHeap_wellFormed' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.prepareFreshHeap_wellFormed
/-- info: 'CloverGC.Differential.Format.validateHeap_completed_complete' depends on axioms: [propext,
 Classical.choice,
 Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.validateHeap_completed_complete
/-- info: 'CloverGC.Differential.Format.writeHeap_completed_complete' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.writeHeap_completed_complete
/-- info: 'CloverGC.Differential.collectPrepared_young_mark_rejected' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.collectPrepared_young_mark_rejected
/-- info: 'CloverGC.Differential.prepareHeap_complete' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.prepareHeap_complete
/-- info: 'CloverGC.Differential.prepareModel_complete' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.prepareModel_complete
/-- info: 'CloverGC.Differential.runCollection_evacuate_complete' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.runCollection_evacuate_complete
/-- info: 'CloverGC.Differential.runCollection_evacuate_serializes' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.runCollection_evacuate_serializes
/-- info: 'CloverGC.Differential.runCollection_mark_complete' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.runCollection_mark_complete
/-- info: 'CloverGC.Differential.runCollection_preserves_original' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.runCollection_preserves_original
/-- info: 'CloverGC.Differential.runCollection_supported_complete' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.runCollection_supported_complete

/- Independent decoding and successful-capture guarantees. -/
/-- info: 'CloverGC.Differential.CellsProjection.length' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Differential.CellsProjection.length
/-- info: 'CloverGC.Differential.Format.decodeImmediate_correct' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.decodeImmediate_correct
/-- info: 'CloverGC.Differential.Format.prepareFreshHeap_correct' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.prepareFreshHeap_correct
/-- info: 'CloverGC.Differential.Format.prepareFreshHeap_ordered' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.prepareFreshHeap_ordered
/-- info: 'CloverGC.Differential.Format.validateHeap_weak_covered' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.validateHeap_weak_covered
/-- info: 'CloverGC.Differential.OriginalProjection.identity' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Differential.OriginalProjection.identity
/-- info: 'CloverGC.Differential.OriginalProjection.weak_layout' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.OriginalProjection.weak_layout
/-- info: 'CloverGC.Differential.captureCopy_object_id' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureCopy_object_id
/-- info: 'CloverGC.Differential.captureEvacuated_allocation_ids' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureEvacuated_allocation_ids
/-- info: 'CloverGC.Differential.captureEvacuated_projects' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureEvacuated_projects
/-- info: 'CloverGC.Differential.captureEvacuated_roots' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureEvacuated_roots
/-- info: 'CloverGC.Differential.captureMark_roots' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureMark_roots
/-- info: 'CloverGC.Differential.captureMarkedOriginal_strong' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureMarkedOriginal_strong
/-- info: 'CloverGC.Differential.captureOriginal_strong' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureOriginal_strong
/-- info: 'CloverGC.Differential.originalKind_correct' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Differential.originalKind_correct
