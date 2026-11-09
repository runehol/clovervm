import CloverGC.Differential.HeapFormat
import CloverGC.WeakRepair
import CloverGC.GenerationalWeakDriver

namespace CloverGC.Differential

inductive Algorithm where
  | mark
  | evacuate
  deriving Repr, BEq

structure PreparedHeap (n k : Nat) where
  initial : Format.HeapSnapshot
  originals : Vector Format.Allocation n -- Indexed by logical identity.
  objectOfAllocation : Vector (Fin n) n -- Indexed by original allocation ID.
  generational : Generational.GenerationalHeap n
  weak : Copying.Weak.Input n k
  roots : List (Fin n)

def PreparedHeap.collectionInput (heap : PreparedHeap n k) (collection : Generational.CollectionKind) :
    Generational.CollectionInput n := ⟨heap.generational, collection, heap.roots⟩
def PreparedHeap.copying (heap : PreparedHeap n k) : Copying.CopyInput n :=
  (heap.collectionInput .full).copyInput

def required (message : String) : Option α → Except String α
  | some value => pure value
  | none => throw message
private def checkedFin (value limit : Nat) : Except String (Fin limit) := do
  if h : value < limit then return ⟨value, h⟩
  else throw "heap conversion: index out of range"
def originalReference (objects : Vector (Fin n) n) (id : Format.AllocationId) :
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
private def originalKind : Format.ObjectState → Except String Generational.OriginalKind
  | .youngOriginal policy _ => pure (.youngMovable (destination policy))
  | .oldOriginal policy _ => pure (.oldMovable (destination policy))
  | .nonmovingOriginal _ => pure .nonmovingOld
  | _ => throw "heap conversion: expected original"
where
  destination : Format.CopyTo → Copying.DestinationKind
    | .ordinary => .ordinary
    | .nonmoving => .nonMoving

/-- Conversion retains its exact input snapshot; the proof is erased at runtime. -/
def prepareModel (initial : Format.HeapSnapshot) :
    Except String (Σ k, { heap : PreparedHeap initial.objects.size k // heap.initial = initial }) := do
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
  let kinds ← originals.mapM fun a => originalKind a.state
  let remembered ← initial.remembered.toList.mapM (originalReference objects)
  let generational ← (Generational.RawGenerationalHeap.mk ⟨children.get⟩ kinds.get remembered).validate
  let ownerUnits ← originals.mapM fun a => a.weak.toList.mapM unitId
  let external ← initial.externalWeak.toList.mapM unitId
  let roots ← strongReferences objects initial.roots
  let weak : Copying.Weak.Input n k := ⟨templates.get, ownerUnits.get, external⟩
  return ⟨k, ⟨⟨initial, originals, objects, generational, weak, roots⟩, rfl⟩⟩

/-- Convert validated wire IDs through the explicit original allocation pairing. -/
def prepareHeap (input : Format.HeapSnapshot) : Except String (Σ n, Σ k, PreparedHeap n k) := do
  let initial ← Format.prepareFreshHeap input
  let ⟨k, heap⟩ ← prepareModel initial
  return ⟨initial.objects.size, k, heap.val⟩

def originalState (initial : Format.ObjectState) (header : Format.Header) :
    Except String Format.ObjectState :=
  match initial, header with
  | .youngOriginal policy _, h => pure (.youngOriginal policy h)
  | .oldOriginal policy _, h => pure (.oldOriginal policy h)
  | .nonmovingOriginal _, .normal marked => pure (.nonmovingOriginal marked)
  | _, _ => throw "heap capture: invalid original state"

def captureMarkedOriginal (heap : PreparedHeap n k) (state : MarkState n)
    (initial : Format.Allocation) : Except String Format.Allocation := do
  let object ← originalReference heap.objectOfAllocation initial.allocationId
  let marked := decide (object ∈ state.marked)
  return { initial with state := ← originalState initial.state (.normal marked) }

/-- Marking retains all cells, units and allocations. Only marks change. -/
def captureMark (heap : PreparedHeap n k) (state : MarkState n) :
    Except String Format.HeapSnapshot := do
  unless state.pending.isEmpty do throw "heap capture: incomplete marking"
  let objects ← heap.initial.objects.toList.mapM (captureMarkedOriginal heap state)
  return { heap.initial with objects := objects.toArray }

/-- Decode observed references directly; do not follow source forwarding. -/
def allocationOfReference (heap : PreparedHeap n k) (state : Copying.CopyState n) :
    Copying.Reference n → Except String Format.AllocationId
  | .source object => pure (heap.originals.get object).allocationId
  | .copied index => do
    let _ ← required "heap capture: unallocated copy reference" state.copies[index]?
    return ⟨n + index⟩

def mergeReferences (convert : α → Except String Format.AllocationId) :
    List Format.Cell → List α → Except String (List Format.Cell)
  | [], [] => pure []
  | [], _ :: _ => throw "heap capture: too many strong references"
  | .reference (some _) :: _, [] => throw "heap capture: too few strong references"
  | .reference (some _) :: rest, ref :: refs => do
    return .reference (some (← convert ref)) :: (← mergeReferences convert rest refs)
  | cell :: rest, refs => return cell :: (← mergeReferences convert rest refs)

def captureCell (convert : Copying.Reference n → Except String Format.AllocationId) :
    Copying.Weak.Cell (Copying.Reference n) → Except String Format.Cell
  | .immediate value => pure (.immediate value)
  | .reference ref => return .reference (← ref.mapM convert)

private def captureUnits (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) (layout : Array Format.WeakUnit) : Except String (Array Format.WeakUnit) :=
  layout.mapM fun unit => do
    let id ← checkedFin unit.unitId.value k
    let convert := allocationOfReference heap copying
    let contents : Format.WeakContents ← match unit.contents, units id with
      | .field _, .field value => pure (.field (← captureCell convert value))
      | .entry _, .entry entry => pure (.entry ⟨← entry.firstGuard.mapM convert,
          ← entry.secondGuard.mapM convert, ← captureCell convert entry.payload⟩)
      | _, _ => throw "heap capture: weak unit kind changed"
    return ⟨unit.unitId, contents⟩

def captureOriginal (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (initial : Format.Allocation) : Except String Format.Allocation := do
  let copying := state.copying
  let selected := (heap.collectionInput collection).selected
  let convert := allocationOfReference heap copying
  let object ← originalReference heap.objectOfAllocation initial.allocationId
  let marked := decide (.source object ∈ copying.marked)
  if heap.copying.movable object && marked then
    throw "heap capture: movable original was marked during evacuation"
  unless selected object do
    unless !marked && (copying.forwarding object).isNone do
      throw "heap capture: unselected original was marked or forwarded"
  let header : Format.Header ← match copying.forwarding object with
    | none => pure (Format.Header.normal marked)
    | some index => do
      unless heap.copying.movable object do throw "heap capture: invalid forwarded original"
      pure (.forwarded (← convert (.copied index)))
  let objectState ← originalState initial.state header
  let strong ← mergeReferences convert initial.strong.toList (state.originalFields object)
  let weak ← if selected object && heap.copying.movable object then pure initial.weak else captureUnits heap copying units initial.weak
  return { initial with state := objectState, strong := strong.toArray, weak := weak }

def captureCopy (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (record : Copying.CopyRecord n) (index : Nat) : Except String Format.Allocation := do
  let copying := state.copying
  let selected := (heap.collectionInput collection).selected
  let convert := allocationOfReference heap copying
  unless selected record.origin && heap.copying.movable record.origin && decide (.copied index ∈ copying.marked) do
    throw "heap capture: invalid or unmarked destination"
  let initial := heap.originals.get record.origin
  let strong ← mergeReferences convert initial.strong.toList record.strongChildren
  let weak ← captureUnits heap copying units initial.weak
  let objectState := match record.destination with
    | .ordinary => Format.ObjectState.ordinaryDestination
    | .nonMoving => Format.ObjectState.nonmovingDestination
  return ⟨⟨s!"obj-{record.origin.val}"⟩, ⟨n + index⟩, objectState, strong.toArray, weak⟩

/-- Project the completed model and retained slot layouts into physical records. -/
def captureStrong (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind) :
    Except String Format.HeapSnapshot := do
  let copying := state.copying
  unless copying.pending.isEmpty do throw "heap capture: incomplete evacuation"
  let convert := allocationOfReference heap copying
  let roots ← mergeReferences convert heap.initial.roots.toList state.roots
  let originals ← heap.initial.objects.toList.mapM (captureOriginal heap state units collection)
  let copies ← copying.copies.zipIdx.mapM fun (record, index) => captureCopy heap state units collection record index
  let objects := (originals ++ copies).toArray
  let result := Format.HeapSnapshot.mk roots.toArray objects (← captureUnits heap copying units heap.initial.externalWeak) heap.initial.remembered
  Format.validateHeap result
  return result

/-- Full weak capture checks both completion queues before projecting actual state. -/
def captureEvacuated (heap : PreparedHeap n k) (state : Copying.Weak.State n k) :
    Except String Format.HeapSnapshot := do
  unless state.pending.isEmpty do throw "heap capture: incomplete weak repair"
  captureStrong heap state.strong state.units .full

def collectPrepared (heap : PreparedHeap n k) (algorithm : Algorithm)
    (collection : Generational.CollectionKind) : Except String Format.HeapSnapshot := do
  if collection == .young then
    unless algorithm == .evacuate do throw "young collection supports evacuation only"
    let weakHeap ← (Generational.Weak.RawWeakHeap.mk heap.generational heap.weak).validate
    let result := (Generational.Weak.collectWeak ⟨weakHeap, .young, heap.roots⟩).1
    unless result.pending.isEmpty do throw "heap capture: incomplete weak repair"
    captureStrong heap result.strong result.units .young
  else
    match algorithm with
    | .mark => captureMark heap (CloverGC.collect heap.copying.heap heap.roots).1
    | .evacuate => captureEvacuated heap (Copying.Weak.collectWeak heap.copying heap.weak heap.roots)

/-- Collection selection is derived from the validated generational heap. -/
def runCollection (input : Format.HeapSnapshot) (algorithm : Algorithm)
    (collection : Generational.CollectionKind := .full) : Except String Format.HeapSnapshot := do
  let ⟨_, _, heap⟩ ← prepareHeap input
  collectPrepared heap algorithm collection

end CloverGC.Differential
