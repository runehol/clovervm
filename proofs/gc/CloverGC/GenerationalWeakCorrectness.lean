import CloverGC.GenerationalWeakDrain

namespace CloverGC.Generational.Weak

open Copying

theorem beforeFinish_strong (input : Input n k) :
    (drainWeak input n (start input)).1.strong = (Generational.evacuate input.strongInput).1 := by
  simpa [Generational.evacuate] using congrArg Prod.fst (drainWeak_strong input n (start input))

theorem beforeFinish_complete (input : Input n k) :
    (drainWeak input n (start input)).1.strong.copying.pending = [] := by
  rw [beforeFinish_strong]
  exact Generational.evacuate_complete input.strongInput

theorem lateVisits_unmentioned (input : Input n k) (state : Copying.Weak.State n k) (ids : List (Fin k))
    (id : Fin k) (not_visited : id ∉ ids) : (lateVisits input state ids).units id = state.units id := by
  induction ids generalizing state with
  | nil => rfl
  | cons head rest ih =>
      have ne : id ≠ head := fun eq => not_visited (by simp [eq])
      have not_rest : id ∉ rest := fun member => not_visited (by simp [member])
      simpa [lateVisits, lateVisit, ne] using ih (lateVisit input state head) not_rest

theorem lateVisits_units (input : Input n k) (state : Copying.Weak.State n k) (ids : List (Fin k))
    (nodup : ids.Nodup) (id : Fin k) :
    (lateVisits input state ids).units id =
      if id ∈ ids then (Copying.Weak.repairUnit (resolve input.strongInput state.strong.copying .late)
        (state.units id)).1 else state.units id := by
  induction ids generalizing state with
  | nil => simp [lateVisits]
  | cons head rest ih =>
      by_cases same : id = head
      · subst id
        have no : head ∉ rest := (List.nodup_cons.mp nodup).1
        have untouched := lateVisits_unmentioned input (lateVisit input state head) rest head no
        simpa [lateVisits, lateVisit] using untouched
      · simpa [lateVisits, lateVisit, same] using
          ih (lateVisit input state head) (List.nodup_cons.mp nodup).2

/-- Every registered unit equals direct late repair of its original template,
    despite partial early writes and skipping units that completed early. -/
theorem collection_units_correct (input : Input n k) (id : Fin k)
    (registered : id ∈ (collectWeak input).1.registered) :
    (collectWeak input).1.units id = desired input (Generational.evacuate input.strongInput).1.copying id := by
  let before := (drainWeak input n (start input)).1
  have h := beforeFinish_units input
  have strong : before.strong.copying = (Generational.evacuate input.strongInput).1.copying :=
    congrArg EvacuationState.copying (beforeFinish_strong input)
  have member : id ∈ before.registered := by
    simpa [collectWeak, finishDeferred, before] using registered
  have nodup : before.pending.Nodup := by
    simpa [collection_late_exact, before] using collection_late_nodup input
  have lookup := lateVisits_units input before before.pending nodup id
  change (lateVisits input before before.pending).units id = _
  rw [lookup]
  split
  · rw [strong]
    exact congrArg Prod.fst (h.compatible id member)
  · rename_i not_pending
    exact h.settled id member not_pending

/-- Unregistered units retain their input storage. In particular, this theorem
    does not silently reset unvisited inactive entries. -/
theorem collection_unregistered_unchanged (input : Input n k) (id : Fin k)
    (unregistered : id ∉ (collectWeak input).1.registered) :
    (collectWeak input).1.units id = (input.heap.data.units.templates id).initial := by
  let before := (drainWeak input n (start input)).1
  have outside : id ∉ before.registered := by
    simpa [collectWeak, finishDeferred, before] using unregistered
  have initialSub : (start input).pending.Sublist (start input).registered := by
    apply rememberOwners_pending_sublist
    exact earlyVisits_pending_sublist input _ _ (List.Sublist.refl [])
  have sub := drainWeak_pending_sublist input n (start input) initialSub
  have not_pending : id ∉ before.pending := fun member => outside (sub.subset member)
  change (lateVisits input before before.pending).units id = _
  rw [lateVisits_unmentioned input before before.pending id not_pending]
  exact (beforeFinish_units input).outside id outside

/-- Every registered active unit preserves its retained guards and payload,
    using graph liveness and old-object retention rather than the resolver as its specification. -/
theorem collection_retained_unit (input : Input n k) (id : Fin k)
    (registered : id ∈ (collectWeak input).1.registered)
    (active : (input.heap.data.units.templates id).active = true)
    (all_retained : ∀ object, some object ∈ (input.heap.data.units.templates id).targets →
      Retained input.strongInput object) :
    (collectWeak input).1.units id = (input.heap.data.units.templates id).map
      (canonical (Generational.evacuate input.strongInput).1.copying) := by
  rw [collection_units_correct input id registered]
  exact congrArg Prod.fst (repair_retained_template input.strongInput _ active all_retained)

/-- A selected dead guard or referenced payload clears the registered unit. -/
theorem collection_dead_unit (input : Input n k) (id : Fin k)
    (registered : id ∈ (collectWeak input).1.registered) (object : Fin n)
    (member : some object ∈ (input.heap.data.units.templates id).targets)
    (inside : input.strongInput.selected object = true) (dead : ¬ Live input.strongInput object) :
    (collectWeak input).1.units id = Copying.Weak.clear (input.heap.data.units.templates id).initial := by
  rw [collection_units_correct input id registered]
  exact congrArg Prod.fst (repair_dead_template input.strongInput _ object member inside dead)

/-- Registered inactive entries clear even when their remaining targets survive.
    Unregistered storage is governed by collection_unregistered_unchanged. -/
theorem collection_inactive_unit (input : Input n k) (id : Fin k)
    (registered : id ∈ (collectWeak input).1.registered)
    (inactive : (input.heap.data.units.templates id).active = false) :
    (collectWeak input).1.units id = Copying.Weak.clear (input.heap.data.units.templates id).initial := by
  rw [collection_units_correct input id registered]
  exact congrArg Prod.fst (Copying.Weak.repair_inactive _ _
    (by simpa [Copying.Weak.Template.initial] using inactive))

theorem collection_weak_queue_empty (input : Input n k) : (collectWeak input).1.pending = [] := rfl

end CloverGC.Generational.Weak
