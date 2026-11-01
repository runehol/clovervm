import CloverGC.CopyDiscoveryProofs

/- Exact dependency guards reject unfinished proofs and custom GC axioms. -/
/-- info: 'CloverGC.Copying.empty_invariant' depends on axioms: [propext, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.empty_invariant
/-- info: 'CloverGC.Copying.discovery_preserves' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.discovery_preserves
/-- info: 'CloverGC.Copying.discovery_source_represents' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.discovery_source_represents
/-- info: 'CloverGC.Copying.discovery_returned_marked' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.discovery_returned_marked
/-- info: 'CloverGC.Copying.discovery_returned_canonical' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.discovery_returned_canonical
/-- info: 'CloverGC.Copying.discovery_fresh_copy' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.discovery_fresh_copy
/-- info: 'CloverGC.Copying.discovery_nonmoving' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.discovery_nonmoving
/-- info: 'CloverGC.Copying.discovery_preserves_copy' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.discovery_preserves_copy
/-- info: 'CloverGC.Copying.discovery_preserves_forwarding' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.discovery_preserves_forwarding
/-- info: 'CloverGC.Copying.no_source_merging' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.no_source_merging
/-- info: 'CloverGC.Copying.unique_representation' depends on axioms: [propext] -/
#guard_msgs in
#print axioms CloverGC.Copying.unique_representation
/-- info: 'CloverGC.Copying.source_rediscovery' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.source_rediscovery
/-- info: 'CloverGC.Copying.returned_rediscovery' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms CloverGC.Copying.returned_rediscovery
