import CloverGC.GenerationalWeakProofs

/- Exact guards exclude custom GC axioms and unfinished proof terms. -/
/-- info: 'CloverGC.Generational.Weak.resolve_unselected' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.resolve_unselected
/-- info: 'CloverGC.Generational.Weak.resolve_old' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.resolve_old
/-- info: 'CloverGC.Generational.Weak.full_resolve' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.full_resolve
/-- info: 'CloverGC.Generational.Weak.resolve_early_not_dead' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.resolve_early_not_dead
/-- info: 'CloverGC.Generational.Weak.resolve_late_not_pending' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.resolve_late_not_pending
/-- info: 'CloverGC.Generational.Weak.resolve_stable' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.resolve_stable
/-- info: 'CloverGC.Generational.Weak.resolved_source_safe' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.resolved_source_safe
/-- info: 'CloverGC.Generational.Weak.resolved_safe' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.resolved_safe
