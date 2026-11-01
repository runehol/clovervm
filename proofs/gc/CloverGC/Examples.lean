import CloverGC.Mark

namespace CloverGC.Examples

/-- Object IDs 0, 1, 2 are A, B, C in the README diagram; 3 is garbage D. -/
def cycleAndGarbage : Heap 4 := ⟨fun object =>
  match object.val with
  | 0 => [1]
  | 1 => [2]
  | 2 => [0]
  | _ => [1]⟩

example : (collect cycleAndGarbage [0, 0]).2 = [0, 1, 2] := by decide
example : (collect cycleAndGarbage [0, 0]).1.marked = [2, 1, 0] := by decide
example : (collect cycleAndGarbage [0, 0]).1.pending = [] := by decide
example : 3 ∉ (collect cycleAndGarbage [0, 0]).1.marked := by decide

def emptyHeap : Heap 0 := ⟨Fin.elim0⟩

example : collect emptyHeap [] = (MarkState.empty, []) := by decide
example : collect cycleAndGarbage [] = (MarkState.empty, []) := by decide

def selfCycle : Heap 1 := ⟨fun _ => [0, 0]⟩

example : (collect selfCycle [0, 0, 0]).2 = [0] := by decide

/-- Duplicate edges and a diamond share object 3; object 4 stays disconnected. -/
def sharedChildren : Heap 5 := ⟨fun object =>
  match object.val with
  | 0 => [0, 1, 1, 2]
  | 1 => [3]
  | 2 => [3]
  | 3 => [0]
  | _ => []⟩

example : (collect sharedChildren [0, 0]).2 = [0, 1, 2, 3] := by decide
example : (collect sharedChildren [2, 1]).2 = [2, 1, 3, 0] := by decide
example : 4 ∉ (collect sharedChildren [0]).1.marked := by decide

def chain : Heap 8 := ⟨fun object =>
  if h : object.val + 1 < 8 then [⟨object.val + 1, h⟩] else []⟩

/-- This uses the entire budget, catching an off-by-one scan bound. -/
example : (collect chain [0]).2 = [0, 1, 2, 3, 4, 5, 6, 7] := by decide
example : (collect chain [0]).1.pending = [] := by decide

-- A deliberately short budget really can leave unfinished work. Completion
-- is proved for collect's n scans, not assumed of every call to drain.
example : (drain chain 7 (seed [0])).1.pending = [7] := by decide

end CloverGC.Examples
