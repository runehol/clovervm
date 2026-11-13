import CloverGC.Differential.HeapValidationCompleteness

namespace CloverGC.Differential.Format

/-- A copied object's weak storage has the source IDs, order and unit kinds. -/
def WeakLayoutMatches (source copy : Array WeakUnit) : Prop :=
  source.size = copy.size ∧ ∀ pair ∈ source.toList.zip copy.toList,
    pair.1.unitId = pair.2.unitId ∧ pair.1.contents.isField = pair.2.contents.isField

private def destinationState : CopyTo → ObjectState
  | .ordinary => .ordinaryDestination
  | .nonmoving => .nonmovingDestination

/-- Independent source/destination correspondence for one logical object. -/
def AllocationPairValid (original : Allocation) (destination : Option Allocation) : Prop :=
  match original.state, destination with
  | .youngOriginal _ (.normal _), none | .oldOriginal _ (.normal _), none
  | .nonmovingOriginal _, none => True
  | .youngOriginal placement (.forwarded target), some copy
  | .oldOriginal placement (.forwarded target), some copy =>
      target = copy.allocationId ∧
      copy.state = destinationState placement ∧
      WeakLayoutMatches original.weak copy.weak
  | _, _ => False

/-- Every semantically matching source/copy pair passes the reciprocal-link check. -/
theorem checkObjectPair_complete (original : Allocation) (destination : Option Allocation)
    (valid : AllocationPairValid original destination) :
    checkObjectPair (some original) destination = .ok () := by
  have copied (copy : Allocation) (placement : CopyTo) (target : AllocationId)
      (moving : movable original.state = some (placement, .forwarded target))
      (targetId : target = copy.allocationId)
      (state : copy.state = destinationState placement)
      (layout : WeakLayoutMatches original.weak copy.weak) :
      checkObjectPair (some original) (some copy) = .ok () := by
    have ordinary : (ObjectState.ordinaryDestination == ObjectState.ordinaryDestination) = true := rfl
    have nonmoving : (ObjectState.nonmovingDestination == ObjectState.nonmovingDestination) = true := rfl
    cases placement <;>
      simp only [checkObjectPair, moving, Option.bind_some, targetId, beq_self_eq_true,
        ↓reduceIte, state, destinationState, ordinary, nonmoving,
        layout.1, bind, Except.bind, pure, Except.pure]
    all_goals
      apply Eq.trans (congrArg (fun result : Except String PUnit =>
        Except.bind result (fun _ => Except.ok ())) (a₂ := .ok PUnit.unit) ?_) rfl
      apply forIn_yields_success
      intro pair member cursor
      cases cursor
      obtain ⟨ids, kinds⟩ := layout.2 pair member
      have sameId : (pair.1.unitId == pair.2.unitId) = true := by
        rw [ids]
        change (pair.2.unitId.value == pair.2.unitId.value) = true
        simp
      cases left : pair.1.contents <;> cases right : pair.2.contents <;>
        simp only [left, right, WeakContents.isField] at kinds
      all_goals simp_all only [Bool.true_eq_false, Bool.false_eq_true]
      all_goals rfl
  cases destination with
  | none =>
      cases state : original.state with
      | youngOriginal placement header | oldOriginal placement header =>
          cases header <;> simp_all [AllocationPairValid, checkObjectPair, movable, bind, Except.bind, pure, Except.pure]
      | nonmovingOriginal marked => simp [checkObjectPair, state, movable, pure, Except.pure]
      | ordinaryDestination | nonmovingDestination => simp [AllocationPairValid, state] at valid
  | some copy =>
      cases state : original.state with
      | youngOriginal placement header | oldOriginal placement header =>
          cases header with
          | normal marked => simp [AllocationPairValid, state] at valid
          | forwarded target =>
              simp only [AllocationPairValid, state] at valid
              exact copied copy placement target (by simp [movable, state]) valid.1 valid.2.1 valid.2.2
      | nonmovingOriginal marked | ordinaryDestination | nonmovingDestination =>
          simp [AllocationPairValid, state] at valid

end CloverGC.Differential.Format
