import CloverGC.Differential.HeapRunnerProofs

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
/-- info: 'CloverGC.Differential.mergeReferences_correct' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.mergeReferences_correct
/-- info: 'CloverGC.Differential.CellsProjection.length' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Differential.CellsProjection.length
/-- info: 'CloverGC.Differential.captureOriginal_strong' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureOriginal_strong
/-- info: 'CloverGC.Differential.captureCopy_strong' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureCopy_strong
/-- info: 'CloverGC.Differential.captureStrong_roots' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureStrong_roots
/-- info: 'CloverGC.Differential.captureEvacuated_roots' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureEvacuated_roots
/-- info: 'CloverGC.Differential.captureMark_roots' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureMark_roots
/-- info: 'CloverGC.Differential.captureMarkedOriginal_strong' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureMarkedOriginal_strong
/-- info: 'CloverGC.Differential.captureUnit_correct' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureUnit_correct
/-- info: 'CloverGC.Differential.captureUnits_correct' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureUnits_correct
/-- info: 'CloverGC.Differential.UnitsProjection.size' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Differential.UnitsProjection.size
/-- info: 'CloverGC.Differential.UnitsProjection.ids' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Differential.UnitsProjection.ids
/-- info: 'CloverGC.Differential.captureStrong_projects' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureStrong_projects
/-- info: 'CloverGC.Differential.captureEvacuated_projects' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.captureEvacuated_projects
