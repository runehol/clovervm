import CloverGC.EvacuationProofs

/- The default build rejects unexpected transitive proof dependencies. -/
/-- info: 'CloverGC.Copying.evacuate_complete' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_complete
/-- info: 'CloverGC.Copying.evacuate_reachable' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_reachable
/-- info: 'CloverGC.Copying.evacuate_roots' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_roots
/-- info: 'CloverGC.Copying.evacuate_children' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_children
/-- info: 'CloverGC.Copying.evacuate_forwarded_iff' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_forwarded_iff
/-- info: 'CloverGC.Copying.evacuate_nonmoving' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_nonmoving
/-- info: 'CloverGC.Copying.evacuate_source_unchanged' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_source_unchanged
/-- info: 'CloverGC.Copying.evacuate_disposal_safe' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_disposal_safe
/-- info: 'CloverGC.Copying.evacuate_trace_nodup' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_trace_nodup
/-- info: 'CloverGC.Copying.evacuate_trace_correct' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_trace_correct
/-- info: 'CloverGC.Copying.evacuate_scans_once' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_scans_once
/-- info: 'CloverGC.Copying.evacuate_scan_bound' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.evacuate_scan_bound
