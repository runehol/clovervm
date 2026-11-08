import CloverGC.Differential.HeapFormat
import CloverGC.WeakRepair

namespace CloverGC.Differential

inductive Algorithm where
  | mark
  | evacuate
  deriving Repr, BEq

structure PreparedHeap (n k : Nat) where
  initial : Format.HeapSnapshot
  originals : Vector Format.Allocation n -- Indexed by logical identity.
  objectOfAllocation : Vector (Fin n) n -- Indexed by original allocation ID.
  copying : Copying.CopyInput n
  weak : Copying.Weak.Input n k
  roots : List (Fin n)

private def required (message : String) : Option α → Except String α
  | some value => pure value
  | none => throw message
private def checkedFin (value limit : Nat) : Except String (Fin limit) := do
  if h : value < limit then return ⟨value, h⟩
  else throw "heap conversion: index out of range"
private def originalReference (objects : Vector (Fin n) n) (id : Format.AllocationId) :
    Except String (Fin n) := do
  return objects.get (← checkedFin id.value n)
private def modelReference (objects : Vector (Fin n) n) (ref : Format.Reference) :
    Except String (Option (Fin n)) := ref.mapM (originalReference objects)
private def modelCell (objects : Vector (Fin n) n) :
    Format.Cell → Except String (Copying.Weak.Cell (Fin n))
  | .immediate value => pure (.immediate value)
  | .reference ref => return .reference (← modelReference objects ref)
private def modelUnit (objects : Vector (Fin n) n) :
    Format.WeakContents → Except String (Copying.Weak.Template n)
  | .field value => return .field (← modelCell objects value)
  | .entry entry => return .entry ⟨← modelReference objects entry.firstGuard,
      ← modelReference objects entry.secondGuard, ← modelCell objects entry.payload⟩
private def strongReferences (objects : Vector (Fin n) n) (cells : Array Format.Cell) :
    Except String (List (Fin n)) := do
  let mut references := []
  for cell in cells do
    if let .reference (some id) := cell then
      references := (← originalReference objects id) :: references
  return references.reverse
private def placement : Format.ObjectState → Except String (Bool × Copying.DestinationKind)
  | .youngOriginal policy _ | .oldOriginal policy _ =>
      pure (true, match policy with | .ordinary => .ordinary | .nonmoving => .nonMoving)
  | .nonmovingOriginal _ => pure (false, .ordinary) -- Unused for nonmoving inputs.
  | _ => throw "heap conversion: expected original"

/-- Convert validated wire IDs through the explicit original allocation pairing. -/
def prepareHeap (input : Format.HeapSnapshot) : Except String (Σ n, Σ k, PreparedHeap n k) := do
  let initial ← Format.prepareFreshHeap input
  let n := initial.objects.size
  let physical := initial.objects.toVector
  let objects ← physical.mapM fun a => do
    let digits := String.ofList (a.objectId.value.toList.drop 4)
    let index ← required "heap conversion: invalid object identity" digits.toNat?
    checkedFin index n
  let mut byObject : Vector (Option Format.Allocation) n := Vector.replicate n none
  for (a, object) in (physical.zip objects).toArray do
    byObject := byObject.set object.val (some a) object.isLt
  let originals ← byObject.mapM (required "heap conversion: missing original")
  let allUnits := (initial.objects.toList.flatMap (fun a => a.weak.toList)) ++ initial.externalWeak.toList
  let k := allUnits.length
  let unitId := fun (unit : Format.WeakUnit) => checkedFin unit.unitId.value k
  let mut unitTable : Vector (Option (Copying.Weak.Template n)) k := Vector.replicate k none
  for unit in allUnits do
    let id ← unitId unit
    unitTable := unitTable.set id.val (some (← modelUnit objects unit.contents)) id.isLt
  let templates ← unitTable.mapM (required "heap conversion: missing weak unit")
  let children ← originals.mapM fun a => strongReferences objects a.strong
  let policies ← originals.mapM fun a => placement a.state
  let ownerUnits ← originals.mapM fun a => a.weak.toList.mapM unitId
  let external ← initial.externalWeak.toList.mapM unitId
  let roots ← strongReferences objects initial.roots
  let copying : Copying.CopyInput n := {
    heap := ⟨children.get⟩
    movable := fun object => (policies.get object).1
    destination := fun object => (policies.get object).2 }
  let weak : Copying.Weak.Input n k := ⟨templates.get, ownerUnits.get, external⟩
  return ⟨n, k, ⟨initial, originals, objects, copying, weak, roots⟩⟩

private def originalState (initial : Format.ObjectState) (header : Format.Header) :
    Except String Format.ObjectState :=
  match initial, header with
  | .youngOriginal policy _, h => pure (.youngOriginal policy h)
  | .oldOriginal policy _, h => pure (.oldOriginal policy h)
  | .nonmovingOriginal _, .normal marked => pure (.nonmovingOriginal marked)
  | _, _ => throw "heap capture: invalid original state"

/-- Marking retains all cells, units and allocations. Only marks change. -/
def captureMark (heap : PreparedHeap n k) (state : MarkState n) :
    Except String Format.HeapSnapshot := do
  unless state.pending.isEmpty do throw "heap capture: incomplete marking"
  let objects ← heap.objectOfAllocation.mapM fun object => do
    let initial := heap.originals.get object
    let marked := decide (object ∈ state.marked)
    return { initial with state := ← originalState initial.state (.normal marked) }
  return { heap.initial with objects := objects.toArray }

/-- Decode observed references directly; do not follow source forwarding. -/
private def allocationOfReference (heap : PreparedHeap n k) (state : Copying.CopyState n) :
    Copying.Reference n → Except String Format.AllocationId
  | .source object => pure (heap.originals.get object).allocationId
  | .copied index => do
    let _ ← required "heap capture: unallocated copy reference" state.copies[index]?
    return ⟨n + index⟩

private def mergeReferences (convert : α → Except String Format.AllocationId) :
    List Format.Cell → List α → Except String (List Format.Cell)
  | [], [] => pure []
  | [], _ :: _ => throw "heap capture: too many strong references"
  | .reference (some _) :: _, [] => throw "heap capture: too few strong references"
  | .reference (some _) :: rest, ref :: refs => do
    return .reference (some (← convert ref)) :: (← mergeReferences convert rest refs)
  | cell :: rest, refs => return cell :: (← mergeReferences convert rest refs)

private def captureCell (convert : Copying.Reference n → Except String Format.AllocationId) :
    Copying.Weak.Cell (Copying.Reference n) → Except String Format.Cell
  | .immediate value => pure (.immediate value)
  | .reference ref => return .reference (← ref.mapM convert)
private def captureUnits (heap : PreparedHeap n k) (state : Copying.Weak.State n k)
    (layout : Array Format.WeakUnit) : Except String (Array Format.WeakUnit) :=
  layout.mapM fun unit => do
    let id ← checkedFin unit.unitId.value k
    let convert := allocationOfReference heap state.strong.copying
    let contents : Format.WeakContents ← match unit.contents, state.units id with
      | .field _, .field value => pure (.field (← captureCell convert value))
      | .entry _, .entry entry => pure (.entry ⟨← entry.firstGuard.mapM convert,
          ← entry.secondGuard.mapM convert, ← captureCell convert entry.payload⟩)
      | _, _ => throw "heap capture: weak unit kind changed"
    return ⟨unit.unitId, contents⟩

/-- Project the completed model and retained slot layouts into physical records. -/
def captureEvacuated (heap : PreparedHeap n k) (state : Copying.Weak.State n k) :
    Except String Format.HeapSnapshot := do
  let copying := state.strong.copying
  unless copying.pending.isEmpty && state.pending.isEmpty do
    throw "heap capture: incomplete evacuation or weak repair"
  let convert := allocationOfReference heap copying
  let roots ← mergeReferences convert heap.initial.roots.toList state.strong.roots
  let originals ← heap.objectOfAllocation.mapM fun object => do
    let initial := heap.originals.get object
    let marked := decide (.source object ∈ copying.marked)
    let header : Format.Header ← match copying.forwarding object with
      | none => pure (Format.Header.normal marked)
      | some index => do
        unless heap.copying.movable object && !marked do throw "heap capture: invalid forwarded original"
        pure (.forwarded (← convert (.copied index)))
    let objectState ← originalState initial.state header
    let strong ← mergeReferences convert initial.strong.toList (state.strong.originalFields object)
    let weak ← if heap.copying.movable object then pure initial.weak else captureUnits heap state initial.weak
    return { initial with state := objectState, strong := strong.toArray, weak := weak }
  let mut objects := originals.toArray
  for (record, index) in copying.copies.zipIdx do
    unless heap.copying.movable record.origin && decide (.copied index ∈ copying.marked) do
      throw "heap capture: invalid or unmarked destination"
    let initial := heap.originals.get record.origin
    let strong ← mergeReferences convert initial.strong.toList record.strongChildren
    let weak ← captureUnits heap state initial.weak
    let objectState := match record.destination with
      | .ordinary => Format.ObjectState.ordinaryDestination
      | .nonMoving => Format.ObjectState.nonmovingDestination
    objects := objects.push ⟨⟨s!"obj-{record.origin.val}"⟩, ⟨n + index⟩, objectState, strong.toArray, weak⟩
  let result := Format.HeapSnapshot.mk roots.toArray objects (← captureUnits heap state heap.initial.externalWeak) heap.initial.rememberedStrong
  Format.validateHeap result
  return result

/-- Full-selection execution delegates to the existing executable definitions. -/
def runCollection (input : Format.HeapSnapshot) (algorithm : Algorithm) :
    Except String Format.HeapSnapshot := do
  let ⟨_, _, heap⟩ ← prepareHeap input
  match algorithm with
  | .mark => captureMark heap (CloverGC.collect heap.copying.heap heap.roots).1
  | .evacuate => captureEvacuated heap (Copying.Weak.collectWeak heap.copying heap.weak heap.roots)

end CloverGC.Differential
