import CloverGC.GenerationalCorrectness

/- Exact guards keep the new generational results free of custom axioms and unfinished proofs. -/
/-- info: 'CloverGC.Generational.full_evacuate' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.full_evacuate
/-- info: 'CloverGC.Generational.evacuate_complete' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Generational.evacuate_complete
/-- info: 'CloverGC.Generational.evacuate_reachable' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Generational.evacuate_reachable
/-- info: 'CloverGC.Generational.evacuate_children' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Generational.evacuate_children
/-- info: 'CloverGC.Generational.evacuate_unselected' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Generational.evacuate_unselected
/-- info: 'CloverGC.Generational.evacuate_disposal_safe' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Generational.evacuate_disposal_safe
/-- info: 'CloverGC.Generational.evacuate_source_unchanged' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Generational.evacuate_source_unchanged
/-- info: 'CloverGC.Generational.evacuate_trace_nodup' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Generational.evacuate_trace_nodup
/-- info: 'CloverGC.Generational.minor_reset_coverage' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Generational.minor_reset_coverage
