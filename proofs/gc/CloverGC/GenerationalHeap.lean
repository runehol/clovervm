import CloverGC.CopyDiscovery

namespace CloverGC.Generational

open Copying

inductive OriginalKind where
  | youngMovable (destination : DestinationKind)
  | oldMovable (destination : DestinationKind)
  | nonmovingOld
  deriving Repr, DecidableEq

def OriginalKind.isYoung : OriginalKind → Bool
  | .youngMovable _ => true
  | _ => false
def OriginalKind.movable : OriginalKind → Bool
  | .nonmovingOld => false
  | _ => true
def OriginalKind.destination : OriginalKind → DestinationKind
  | .youngMovable d | .oldMovable d => d
  | .nonmovingOld => .ordinary

structure RawGenerationalHeap (n : Nat) where
  graph : Heap n
  objectKind : Fin n → OriginalKind
  rememberedOwners : List (Fin n)

def RawGenerationalHeap.isYoung (heap : RawGenerationalHeap n) (o : Fin n) : Bool :=
  (heap.objectKind o).isYoung
abbrev RawGenerationalHeap.isOld (heap : RawGenerationalHeap n) (o : Fin n) : Prop :=
  heap.isYoung o = false

structure WellFormed (heap : RawGenerationalHeap n) : Prop where
  remembered_unique : heap.rememberedOwners.Nodup
  remembered_old : ∀ owner ∈ heap.rememberedOwners, heap.isOld owner
  old_to_young_covered : ∀ owner child, heap.isOld owner →
    child ∈ heap.graph.strongChildren owner → heap.isYoung child = true →
    owner ∈ heap.rememberedOwners

instance (heap : RawGenerationalHeap n) : Decidable (WellFormed heap) := by
  exact decidable_of_iff
    (heap.rememberedOwners.Nodup ∧
      (∀ owner ∈ heap.rememberedOwners, heap.isOld owner) ∧
      (∀ owner child, heap.isOld owner → child ∈ heap.graph.strongChildren owner →
        heap.isYoung child = true → owner ∈ heap.rememberedOwners))
    ⟨fun ⟨a, b, c⟩ => ⟨a, b, c⟩, fun h => ⟨h.1, h.2, h.3⟩⟩

structure GenerationalHeap (n : Nat) where
  data : RawGenerationalHeap n
  valid : WellFormed data

def RawGenerationalHeap.validate (heap : RawGenerationalHeap n) :
    Except String (GenerationalHeap n) :=
  if valid : WellFormed heap then pure ⟨heap, valid⟩
  else throw "generational heap: remembered owners must be unique and old and cover every old-to-young edge"

end CloverGC.Generational
