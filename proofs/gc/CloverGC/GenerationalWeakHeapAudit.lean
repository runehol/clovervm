import CloverGC.GenerationalWeakHeap

/- Unit ownership and coverage consequences introduce no custom GC axioms. -/
/-- info: 'CloverGC.Generational.Weak.WeakHeap.owner_unique' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.WeakHeap.owner_unique
/-- info: 'CloverGC.Generational.Weak.WeakHeap.owner_not_external' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.WeakHeap.owner_not_external
/-- info: 'CloverGC.Generational.Weak.WeakHeap.unremembered_targets_old' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Generational.Weak.WeakHeap.unremembered_targets_old
