import CloverGC.Differential.HeapFormatProofs

/- Guard transitive dependencies of the executable validators' coverage proofs. -/
/-- info: 'CloverGC.Differential.Format.validateHeap_weak_covered' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.validateHeap_weak_covered
/-- info: 'CloverGC.Differential.Format.prepareFreshHeap_weak_covered' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.prepareFreshHeap_weak_covered
/-- info: 'CloverGC.Differential.Format.checkReference_correct' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.checkReference_correct
/-- info: 'CloverGC.Differential.Format.checkFreshHeap_correct' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Differential.Format.checkFreshHeap_correct
