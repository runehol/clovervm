import CloverGC.WeakRepairProofs

/- The normal build checks these transitive dependencies; no custom collector
   axiom or unfinished proof is permitted. -/
/-- info: 'CloverGC.Copying.Weak.resolve_early_not_dead' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.resolve_early_not_dead
/-- info: 'CloverGC.Copying.Weak.resolve_late_not_pending' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.resolve_late_not_pending
/-- info: 'CloverGC.Copying.Weak.repair_late_complete' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.repair_late_complete
/-- info: 'CloverGC.Copying.Weak.resolve_stable' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.resolve_stable
/-- info: 'CloverGC.Copying.Weak.early_unit_compatible' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.early_unit_compatible
/-- info: 'CloverGC.Copying.Weak.early_complete_correct' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.early_complete_correct
/-- info: 'CloverGC.Copying.Weak.collectWeak_strong' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collectWeak_strong
/-- info: 'CloverGC.Copying.Weak.collection_registered_iff' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collection_registered_iff
/-- info: 'CloverGC.Copying.Weak.collection_registered_nodup' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collection_registered_nodup
/-- info: 'CloverGC.Copying.Weak.collection_late_exact' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collection_late_exact
/-- info: 'CloverGC.Copying.Weak.collection_late_nodup' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collection_late_nodup
/-- info: 'CloverGC.Copying.Weak.collection_units_correct' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collection_units_correct
/-- info: 'CloverGC.Copying.Weak.collection_live_unit' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collection_live_unit
/-- info: 'CloverGC.Copying.Weak.collection_dead_unit' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collection_dead_unit
/-- info: 'CloverGC.Copying.Weak.collection_disposal_safe' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collection_disposal_safe
/-- info: 'CloverGC.Copying.Weak.collection_weak_queue_empty' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.collection_weak_queue_empty
/-- info: 'CloverGC.Copying.Weak.earlyVisit_deferred_iff' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.earlyVisit_deferred_iff
/-- info: 'CloverGC.Copying.Weak.repair_dead_dominates' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.Weak.repair_dead_dominates
