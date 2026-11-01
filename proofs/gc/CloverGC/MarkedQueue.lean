import Std

namespace CloverGC.MarkedQueue

/-- A proof view of the shared mark-before-enqueue operation. The executable
    mark/copy models keep their own state records; these facts concern only
    their marked/pending lists, for any decidable element type. -/
def enqueue [DecidableEq α] (object : α) (marked pending : List α) : List α × List α :=
  if object ∈ marked then (marked, pending)
  else (object :: marked, pending ++ [object])

theorem marked_mem [DecidableEq α] (object x : α) (marked pending : List α) :
    x ∈ (enqueue object marked pending).1 ↔ x = object ∨ x ∈ marked := by
  unfold enqueue
  split <;> simp_all

theorem pending_mem [DecidableEq α] (object x : α) (marked pending : List α) :
    x ∈ (enqueue object marked pending).2 ↔
      x ∈ pending ∨ (x = object ∧ object ∉ marked) := by
  unfold enqueue
  split <;> simp_all

theorem nodup [DecidableEq α] (object : α) (marked pending : List α)
    (marked_nodup : marked.Nodup) (pending_nodup : pending.Nodup)
    (included : ∀ x ∈ pending, x ∈ marked) :
    (enqueue object marked pending).1.Nodup ∧ (enqueue object marked pending).2.Nodup := by
  unfold enqueue
  split
  · exact ⟨marked_nodup, pending_nodup⟩
  · constructor
    · simp_all
    · simp only [List.nodup_append, List.nodup_cons, List.nodup_nil,
        List.not_mem_nil, not_false_eq_true, and_self]
      exact ⟨pending_nodup, by grind⟩

theorem included [DecidableEq α] (object : α) (marked pending : List α)
    (before : ∀ x ∈ pending, x ∈ marked) :
    ∀ x ∈ (enqueue object marked pending).2, x ∈ (enqueue object marked pending).1 := by
  intro x member
  rw [pending_mem] at member
  rw [marked_mem]
  rcases member with member | ⟨rfl, _⟩
  · exact Or.inr (before x member)
  · exact Or.inl rfl

end CloverGC.MarkedQueue
