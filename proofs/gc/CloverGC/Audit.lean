import CloverGC.MarkProofs

/- Check transitive dependencies of the public claims. No `sorryAx` or custom
   collector axiom is acceptable; standard Lean logical axioms are permitted.
   Guard the messages so an unexpected dependency fails the normal build. -/
/-- info: 'CloverGC.collect_complete' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.collect_complete
/-- info: 'CloverGC.collect_correct' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.collect_correct
/-- info: 'CloverGC.collect_trace_nodup' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.collect_trace_nodup
/-- info: 'CloverGC.collect_scans_once' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.collect_scans_once
/-- info: 'CloverGC.collect_skips_unreachable' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.collect_skips_unreachable
/-- info: 'CloverGC.collect_scan_bound' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.collect_scan_bound
