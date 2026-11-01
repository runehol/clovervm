import CloverGC.CopyDiscoveryProofs

namespace CloverGC.Copying.Examples

/-- Source 2 is NonMoving; source 0 moves to NonMoving destination storage.
    Self-links, duplicate edges, and child 1 must initially remain source refs. -/
def input : CopyInput 4 := {
  heap := ⟨fun object => match object.val with
    | 0 => [0, 1, 1]
    | 1 => [2]
    | 2 => []
    | _ => [0]⟩
  movable := fun object => object.val != 2
  destination := fun object =>
    if object.val = 0 then .nonMoving else .ordinary
}

-- Allocate source 1 first: destination index 0 must not mean source identity 0.
def first := discoverReference input .empty (.source 1)
def second := discoverReference input first.1 (.source 0)
def fixed := discoverReference input second.1 (.source 2)

example : first.2 = .copied 0 := by decide
example : first.1.copies.map CopyRecord.origin = [1] := by decide
example : first.1.forwarding 1 = some 0 := by decide
example : first.1.forwarding 0 = none := by decide
example : second.2 = .copied 1 := by decide
example : second.1.copies.map CopyRecord.origin = [1, 0] := by decide
example : second.1.forwarding 1 = some 0 ∧ second.1.forwarding 0 = some 1 := by decide
example : second.1.copies.map CopyRecord.destination = [.ordinary, .nonMoving] := by decide
example : second.1.pending = [.copied 0, .copied 1] := by decide
example : second.1.copies[1]?.map CopyRecord.strongChildren =
    some [.source 0, .source 1, .source 1] := by decide
example : fixed.2 = .source 2 := by decide
example : fixed.1.copies.length = 2 := by decide
example : fixed.1.forwarding 2 = none := by decide
example : fixed.1.pending = [.copied 0, .copied 1, .source 2] := by decide

def repeated := discoverReference input fixed.1 (.source 0)
def repeatedFixed := discoverReference input fixed.1 (.source 2)
def repaired := discoverReference input fixed.1 (.copied 1)

example : repeated.2 = .copied 1 ∧ repeated.1.copies = fixed.1.copies ∧
    repeated.1.pending = fixed.1.pending := by decide
example : repeatedFixed.2 = .source 2 ∧ repeatedFixed.1.pending = fixed.1.pending := by decide
example : repaired.2 = .copied 1 ∧ repaired.1.marked = fixed.1.marked ∧
    repaired.1.pending = fixed.1.pending := by decide

-- The output of actual successive discoveries satisfies the invariant, rather
-- than supplying forwarding consistency or canonical marks as fixture axioms.
theorem first_valid : CopyInvariant input first.1 :=
  discovery_preserves input .empty (.source 1) (empty_invariant input) trivial

theorem second_valid : CopyInvariant input second.1 :=
  discovery_preserves input first.1 (.source 0) first_valid trivial

theorem fixed_valid : CopyInvariant input fixed.1 :=
  discovery_preserves input second.1 (.source 2) second_valid trivial

example : discoverReference input fixed.1 (.source 0) = (fixed.1, .copied 1) := by
  apply represented_source_rediscovery input fixed.1 0 (.copied 1) fixed_valid
  · change fixed.1.forwarding 0 = some 1
    decide
  · decide

-- Model the scheduling situation after earlier work has been consumed. Marks
-- persist, so rediscovery must not requeue an already-scanned canonical object.
-- This does not claim to implement/prove the scan that produced that situation.
def drained : CopyState 4 := { fixed.1 with pending := [] }

theorem drained_valid : CopyInvariant input drained := {
  fixed_valid with
  ledger := {
    forwarding_valid := fixed_valid.ledger.forwarding_valid
    copies_forwarded := fixed_valid.ledger.copies_forwarded
    copies_faithful := fixed_valid.ledger.copies_faithful
  }
  pending_nodup := by simp [drained]
  pending_marked := by simp [drained]
}

example : discoverReference input drained (.source 0) = (drained, .copied 1) := by
  apply represented_source_rediscovery input drained 0 (.copied 1) drained_valid
  · change drained.forwarding 0 = some 1
    decide
  · decide

example : (discoverReference input drained (.source 2)).1.pending = [] := by decide

-- Invalid destination indices are representable but cannot satisfy the theorem
-- precondition. They are not accidentally proved to be valid repaired refs.
example : ¬ ValidReference fixed.1 (.copied 2) := by
  have get : fixed.1.copies[2]? = none := by decide
  simp [ValidReference, get]

end CloverGC.Copying.Examples
