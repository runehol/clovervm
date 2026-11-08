import CloverGC.Differential.HeapRunner

/- Capture preservation applies to executable output, not just the logical collector. -/
/-- info: 'CloverGC.Differential.Format.prepareFreshHeap_objects_perm' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.prepareFreshHeap_objects_perm
/-- info: 'CloverGC.Differential.prepareHeap_initial' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.prepareHeap_initial
/-- info: 'CloverGC.Differential.captureMark_identities' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureMark_identities
/-- info: 'CloverGC.Differential.captureEvacuated_identities' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureEvacuated_identities
/-- info: 'CloverGC.Differential.captureEvacuated_allocation_ids' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureEvacuated_allocation_ids
/-- info: 'CloverGC.Differential.runCollection_original_prefix' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.runCollection_original_prefix
/-- info: 'CloverGC.Differential.runCollection_preserves_original' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.runCollection_preserves_original
/-- info: 'CloverGC.Differential.allocationOfReference_correct' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.allocationOfReference_correct
/-- info: 'CloverGC.Differential.captureCell_correct' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureCell_correct
/-- info: 'CloverGC.Differential.captureCell_succeeds' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureCell_succeeds
