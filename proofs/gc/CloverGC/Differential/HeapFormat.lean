import Lean.Data.Json.Parser
import Lean.Data.Json.Printer
import Std.Data.HashSet
import Init.Data.Array.Sort.Lemmas

namespace CloverGC.Differential.Format

structure ObjectId where
  value : String
  deriving Repr, BEq, DecidableEq
structure AllocationId where
  value : Nat
  deriving Repr, BEq, DecidableEq, ReflBEq, LawfulBEq
structure UnitId where
  value : Nat
  deriving Repr, BEq, DecidableEq

abbrev Reference := Option AllocationId
inductive Cell where
  | immediate (value : Int)
  | reference (target : Reference)
  deriving Repr, BEq, DecidableEq
structure Entry where
  firstGuard : Reference
  secondGuard : Reference
  payload : Cell
  deriving Repr, BEq, DecidableEq
inductive WeakContents where
  | field (value : Cell)
  | entry (value : Entry)
  deriving Repr, BEq, DecidableEq
structure WeakUnit where
  unitId : UnitId
  contents : WeakContents
  deriving Repr, BEq, DecidableEq

inductive CopyTo where
  | ordinary
  | nonmoving
  deriving Repr, BEq, DecidableEq
inductive Header where
  | normal (marked : Bool)
  | forwarded (forwardedTo : AllocationId)
  deriving Repr, BEq, DecidableEq
inductive ObjectState where
  | youngOriginal (copyTo : CopyTo) (header : Header)
  | oldOriginal (copyTo : CopyTo) (header : Header)
  | nonmovingOriginal (marked : Bool)
  | ordinaryDestination
  | nonmovingDestination
  deriving Repr, BEq, DecidableEq
structure Allocation where
  objectId : ObjectId
  allocationId : AllocationId
  state : ObjectState
  strong : Array Cell
  weak : Array WeakUnit
  deriving Repr, BEq, DecidableEq
structure HeapSnapshot where
  roots : Array Cell
  objects : Array Allocation
  externalWeak : Array WeakUnit
  remembered : Array AllocationId
  deriving Repr, BEq, DecidableEq

/-- Includes every stored reference, even guards and payloads of inactive entries. -/
def WeakContents.targets : WeakContents → List AllocationId
  | .field (.immediate _) => []
  | .field (.reference target) => target.toList
  | .entry e => e.firstGuard.toList ++ e.secondGuard.toList ++
      (match e.payload with | .reference target => target.toList | .immediate _ => [])

def ObjectState.isYoung : ObjectState → Bool
  | .youngOriginal _ _ => true
  | _ => false

/-- Coverage uses allocation identity, independently of array order.
    External weak units have no owner and impose no remembered membership. -/
def WeakRememberedCovered (heap : HeapSnapshot) : Prop :=
  ∀ owner ∈ heap.objects.toList, owner.state.isYoung = false →
    ∀ unit ∈ owner.weak.toList, ∀ target ∈ unit.contents.targets,
      ∀ allocation ∈ heap.objects.toList, allocation.allocationId = target →
        allocation.state.isYoung = true → owner.allocationId ∈ heap.remembered.toList

instance (heap : HeapSnapshot) : Decidable (WeakRememberedCovered heap) := by
  unfold WeakRememberedCovered
  infer_instance

private def failAt (path message : String) : Except String α :=
  .error s!"{path}: {message}"
private def indexPath (path : String) (index : Nat) : String := s!"{path}[{index}]"
private def uint64Max : Nat := 18446744073709551615
private def int64Min : Int := -9223372036854775808
private def int64Max : Int := 9223372036854775807

private def get (json : Lean.Json) (path key : String) : Except String Lean.Json :=
  json.getObjVal? key |>.mapError (fun message => s!"{path}.{key}: {message}")
private def string (json : Lean.Json) (path : String) : Except String String :=
  json.getStr?.mapError (fun _ => s!"{path}: expected string")
private def boolean (json : Lean.Json) (path : String) : Except String Bool :=
  json.getBool?.mapError (fun _ => s!"{path}: expected boolean")
private def array (json : Lean.Json) (path : String) : Except String (Array Lean.Json) :=
  json.getArr?.mapError (fun _ => s!"{path}: expected array")
private def integer (json : Lean.Json) (path : String) : Except String Int :=
  json.getInt?.mapError (fun _ => s!"{path}: expected integer")
private def natural (json : Lean.Json) (path : String) : Except String Nat := do
  let value ← integer json path
  unless value ≥ 0 && value ≤ (uint64Max : Int) do
    failAt path "expected nonnegative integer in uint64 range"
  return value.toNat
private def reference (json : Lean.Json) (path : String) : Except String Reference := do
  if json.isNull then return none
  return some ⟨← natural json path⟩
private def cell (json : Lean.Json) (path : String) : Except String Cell := do
  if (json.getObjVal? "imm").isOk then
    let value ← integer (← get json path "imm") (path ++ ".imm")
    return .immediate value
  return .reference (← reference (← get json path "ref") (path ++ ".ref"))
private def cells (json : Lean.Json) (path : String) : Except String (Array Cell) := do
  let mut result := #[]
  for item in ← array json path do
    result := result.push (← cell item (indexPath path result.size))
  return result
private def units (json : Lean.Json) (path : String) : Except String (Array WeakUnit) := do
  let mut result := #[]
  for item in ← array json path do
    let p := indexPath path result.size
    let kind ← string (← get item p "kind") (p ++ ".kind")
    let contents ← if kind == "field" then do
      pure (.field (← cell (← get item p "value") (p ++ ".value")))
    else if kind == "entry" then do
      let guards ← array (← get item p "guards") (p ++ ".guards")
      match guards.toList with
      | [first, second] =>
        pure (.entry ⟨← reference first (p ++ ".guards[0]"),
                      ← reference second (p ++ ".guards[1]"),
                      ← cell (← get item p "payload") (p ++ ".payload")⟩)
      | _ => failAt (p ++ ".guards") "expected exactly two guards"
    else failAt (p ++ ".kind") "unknown weak unit kind"
    result := result.push ⟨⟨← natural (← get item p "unit_id") (p ++ ".unit_id")⟩, contents⟩
  return result
private def header (json : Lean.Json) (path : String) : Except String Header := do
  let kind ← string (← get json path "kind") (path ++ ".kind")
  if kind == "normal" then
    return .normal (← boolean (← get json path "marked") (path ++ ".marked"))
  if kind == "forwarded" then
    return .forwarded ⟨← natural (← get json path "forwarded_to") (path ++ ".forwarded_to")⟩
  failAt (path ++ ".kind") "unknown header kind"
private def objectState (json : Lean.Json) (path : String) : Except String ObjectState := do
  let kind ← string (← get json path "kind") (path ++ ".kind")
  if kind == "young_original" || kind == "old_original" then
    let placement ← string (← get json path "copy_to") (path ++ ".copy_to")
    let copyTo ← if placement == "ordinary" then pure CopyTo.ordinary
      else if placement == "nonmoving" then pure CopyTo.nonmoving
      else failAt (path ++ ".copy_to") "unknown placement"
    let h ← header (← get json path "header") (path ++ ".header")
    return if kind == "young_original" then .youngOriginal copyTo h else .oldOriginal copyTo h
  if kind == "nonmoving_original" then
    return .nonmovingOriginal (← boolean (← get json path "marked") (path ++ ".marked"))
  if kind == "ordinary_destination" then return .ordinaryDestination
  if kind == "nonmoving_destination" then return .nonmovingDestination
  failAt (path ++ ".kind") "unknown object state"

private def objectNumber (id : ObjectId) (path : String) : Except String Nat := do
  unless id.value.startsWith "obj-" do failAt path "expected canonical obj-N identity"
  let digits := String.ofList (id.value.toList.drop 4)
  unless !digits.isEmpty && digits.toList.all (fun c => '0' ≤ c && c ≤ '9') do
    failAt path "expected canonical obj-N identity"
  if digits.length > 1 && digits.startsWith "0" then failAt path "expected canonical obj-N identity"
  let some number := digits.toNat? | failAt path "invalid object identity"
  if number > uint64Max then failAt path "object identity out of range"
  return number
private def isDestination : ObjectState → Bool
  | .ordinaryDestination | .nonmovingDestination => true
  | _ => false
private def movable : ObjectState → Option (CopyTo × Header)
  | .youngOriginal copyTo h | .oldOriginal copyTo h => some (copyTo, h)
  | _ => none
private def checkReference (ref : Reference) (count : Nat) (path : String) : Except String Unit := do
  if let some id := ref then
    unless id.value < count do failAt path "reference names no allocation"
private def checkCell (value : Cell) (count : Nat) (path : String) : Except String Unit := do
  match value with
  | .immediate i =>
    unless i ≥ int64Min && i ≤ int64Max do failAt path "expected signed 64-bit integer"
  | .reference ref => checkReference ref count (path ++ ".ref")
private inductive UnitStorage where
  | original | destination | external
  deriving BEq
private def checkUnits (values : Array WeakUnit) (path : String) (storage : UnitStorage)
    (count : Nat) (seen : Std.HashSet Nat) : Except String (Std.HashSet Nat) := do
  let mut seen := seen
  let mut seenEntry := false
  for (unit, i) in values.toList.zipIdx do
    let p := indexPath path i
    if storage != .destination then
      if seen.contains unit.unitId.value then failAt p "duplicate logical weak unit"
      seen := seen.insert unit.unitId.value
    match unit.contents with
    | .field value =>
      if storage != .external && seenEntry then failAt p "owner fields must precede entries"
      checkCell value count (p ++ ".value")
    | .entry entry =>
      seenEntry := true
      checkReference entry.firstGuard count (p ++ ".guards[0]")
      checkReference entry.secondGuard count (p ++ ".guards[1]")
      checkCell entry.payload count (p ++ ".payload")
  return seen

/-- Validate structural preconditions of an allocation-ordered snapshot. -/
private def validateStructure (heap : HeapSnapshot) : Except String Unit := do
  let count := heap.objects.size
  let originalCount := heap.objects.foldl (fun n a => if isDestination a.state then n else n + 1) 0
  let mut origins : Array (Option Allocation) := Array.replicate originalCount none
  let mut copies : Array (Option Allocation) := Array.replicate originalCount none
  for (value, i) in heap.roots.toList.zipIdx do checkCell value count (indexPath "$.roots" i)
  let mut seen ← checkUnits heap.externalWeak "$.external_weak" .external count {}
  for (a, i) in heap.objects.toList.zipIdx do
    let p := indexPath "$.objects" i
    unless a.allocationId.value == i do failAt p "allocation IDs must be unique and dense"
    let logical ← objectNumber a.objectId (p ++ ".object_id")
    if isDestination a.state then
      unless a.allocationId.value ≥ originalCount do failAt p "original IDs must precede destinations"
      if h : logical < copies.size then
        if copies[logical].isSome then failAt p "duplicate destination identity"
        copies := copies.set logical (some a)
      else failAt p "object identities must be dense"
    else
      unless a.allocationId.value < originalCount do failAt p "original IDs must precede destinations"
      if h : logical < origins.size then
        if origins[logical].isSome then failAt p "duplicate original identity"
        origins := origins.set logical (some a)
      else failAt p "object identities must be dense"
    for (value, j) in a.strong.toList.zipIdx do checkCell value count (indexPath (p ++ ".strong") j)
    seen ← checkUnits a.weak (p ++ ".weak") (if isDestination a.state then .destination else .original) count seen
  for id in seen.toList do
    unless id < seen.size do failAt "$.weak" "logical unit IDs must be dense"
  let mut remembered : Std.HashSet Nat := {}
  for id in heap.remembered do
    let some owner := heap.objects[id.value]? | failAt "$.remembered" "unknown owner"
    match owner.state with
    | .oldOriginal _ _ | .nonmovingOriginal _ => pure ()
    | _ => failAt "$.remembered" "remembered owners must be old originals"
    if remembered.contains id.value then failAt "$.remembered" "duplicate remembered owner"
    remembered := remembered.insert id.value
  for a in heap.objects do
    if !(match a.state with | .youngOriginal _ _ => true | _ => false) then
      for value in a.strong do
        if let .reference (some id) := value then
          if let some (.youngOriginal _ _) := heap.objects[id.value]?.map Allocation.state then
            unless remembered.contains a.allocationId.value do
              failAt "$.objects.strong" "unremembered old-to-young strong edge"
  for i in [:originalCount] do
    let some original := origins[i]! | failAt "$.objects" "missing original identity"
    let forwarded := (movable original.state).bind fun (_, h) =>
      match h with | .forwarded id => some id | _ => none
    match forwarded, copies[i]! with
    | none, none => pure ()
    | some target, some copy =>
      unless target == copy.allocationId do failAt "$.objects" "forwarding must name the same object's destination"
      let some (placement, _) := movable original.state | failAt "$.objects" "nonmoving original has destination"
      let expected := match placement with
        | .ordinary => ObjectState.ordinaryDestination
        | .nonmoving => ObjectState.nonmovingDestination
      unless copy.state == expected do failAt "$.objects" "destination disagrees with copy_to"
      unless original.weak.size == copy.weak.size do failAt "$.objects" "destination weak layout size differs"
      for (a, b) in original.weak.toList.zip copy.weak.toList do
        let sameKind := match a.contents, b.contents with
          | .field _, .field _ | .entry _, .entry _ => true
          | _, _ => false
        unless a.unitId == b.unitId && sameKind do failAt "$.objects" "destination weak layout differs"
    | _, _ => failAt "$.objects" "destination and original require reciprocal forwarding"

private def orderedHeap (input : HeapSnapshot) : HeapSnapshot :=
  { input with objects := input.objects.mergeSort (fun a b => a.allocationId.value ≤ b.allocationId.value) }

private theorem orderedHeap_coverage (input : HeapSnapshot) :
    WeakRememberedCovered (orderedHeap input) ↔ WeakRememberedCovered input := by
  simp only [WeakRememberedCovered, orderedHeap, ← Array.mem_def, Array.mem_mergeSort]

private def validateAndOrder (input : HeapSnapshot) :
    Except String { heap : HeapSnapshot // WeakRememberedCovered heap } := do
  let heap := orderedHeap input
  validateStructure heap
  if covered : WeakRememberedCovered heap then pure ⟨heap, covered⟩
  else throw "$.objects.weak: unremembered old-to-young weak edge"

/-- Validate both fresh and completed snapshot structure, without collecting. -/
def validateHeap (heap : HeapSnapshot) : Except String Unit := do
  let _ ← validateAndOrder heap

private def checkFreshHeap (heap : HeapSnapshot) : Except String Unit := do
  for a in heap.objects do
    match a.state with
    | .youngOriginal _ (.normal false) | .oldOriginal _ (.normal false)
    | .nonmovingOriginal false => pure ()
    | _ => failAt "$.objects" "fresh heap requires only normal unmarked originals"

/-- Validate fresh-run preconditions and return the ordered input. -/
def prepareFreshHeap (input : HeapSnapshot) : Except String HeapSnapshot := do
  let checked ← validateAndOrder input
  checkFreshHeap checked.val
  return checked.val

private theorem validateAndOrder_value (input : HeapSnapshot)
    (checked : { heap : HeapSnapshot // WeakRememberedCovered heap })
    (accepted : validateAndOrder input = .ok checked) : checked.val = orderedHeap input := by
  unfold validateAndOrder at accepted
  cases structureResult : validateStructure (orderedHeap input) with
  | error message => simp [structureResult, bind, Except.bind] at accepted
  | ok value =>
      simp only [structureResult, bind, Except.bind] at accepted
      split at accepted
      · simp only [pure, Except.pure, Except.ok.injEq] at accepted
        cases accepted
        rfl
      · contradiction

/-- Successful public validation certifies coverage of the supplied snapshot. -/
theorem validateHeap_weak_covered (heap : HeapSnapshot)
    (accepted : validateHeap heap = .ok ()) : WeakRememberedCovered heap := by
  unfold validateHeap at accepted
  cases result : validateAndOrder heap with
  | error message => simp [result, bind, Except.bind] at accepted
  | ok checked =>
      have covered := checked.property
      rw [validateAndOrder_value heap checked result] at covered
      exact (orderedHeap_coverage heap).mp covered

/-- Successful fresh preparation certifies coverage of the returned snapshot. -/
theorem prepareFreshHeap_weak_covered (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) : WeakRememberedCovered heap := by
  unfold prepareFreshHeap at accepted
  cases result : validateAndOrder input with
  | error message => simp [result, bind, Except.bind] at accepted
  | ok checked =>
      cases fresh : checkFreshHeap checked.val with
      | error message => simp [result, fresh, bind, Except.bind] at accepted
      | ok value =>
          simp [result, fresh, bind, Except.bind, pure, Except.pure] at accepted
          subst heap
          exact checked.property

/-- A separate restriction for a single fresh full-selection run. -/
def validateFreshHeap (heap : HeapSnapshot) : Except String Unit := do
  let _ ← prepareFreshHeap heap

/-- Read generated JSON with the bundled parser, then validate the heap. -/
def parseHeap (bytes : ByteArray) : Except String HeapSnapshot := do
  let some text := String.fromUTF8? bytes | throw "invalid UTF-8"
  let json ← Lean.Json.parse text
  unless (← string (← get json "$" "format") "$.format") == "clover-gc-heap" do
    failAt "$.format" "unsupported format"
  unless (← natural (← get json "$" "version") "$.version") == 2 do
    failAt "$.version" "unsupported version"
  let mut objects := #[]
  for item in ← array (← get json "$" "objects") "$.objects" do
    let p := indexPath "$.objects" objects.size
    objects := objects.push ⟨⟨← string (← get item p "object_id") (p ++ ".object_id")⟩,
      ⟨← natural (← get item p "allocation_id") (p ++ ".allocation_id")⟩,
      ← objectState (← get item p "state") (p ++ ".state"),
      ← cells (← get item p "strong") (p ++ ".strong"),
      ← units (← get item p "weak") (p ++ ".weak")⟩
  let heap := HeapSnapshot.mk (← cells (← get json "$" "roots") "$.roots") objects
    (← units (← get json "$" "external_weak") "$.external_weak")
    (← (← array (← get json "$" "remembered") "$.remembered").mapM fun value =>
      return ⟨← natural value "$.remembered"⟩)
  return (← validateAndOrder heap).val

private def natJson (n : Nat) : Lean.Json := .num (.fromNat n)
private def refJson : Reference → Lean.Json
  | none => .null
  | some id => natJson id.value
private def cellJson : Cell → Lean.Json
  | .immediate value => .mkObj [("imm", .num (.fromInt value))]
  | .reference ref => .mkObj [("ref", refJson ref)]
private def unitsJson (units : Array WeakUnit) : Lean.Json := .arr (units.map fun unit =>
  match unit.contents with
  | .field value => .mkObj [("unit_id", natJson unit.unitId.value), ("kind", .str "field"), ("value", cellJson value)]
  | .entry entry => .mkObj [("unit_id", natJson unit.unitId.value), ("kind", .str "entry"),
      ("guards", .arr #[refJson entry.firstGuard, refJson entry.secondGuard]), ("payload", cellJson entry.payload)])
private def headerJson : Header → Lean.Json
  | .normal marked => .mkObj [("kind", .str "normal"), ("marked", .bool marked)]
  | .forwarded target => .mkObj [("kind", .str "forwarded"), ("forwarded_to", natJson target.value)]
private def stateJson : ObjectState → Lean.Json
  | .youngOriginal copyTo h => moving "young_original" copyTo h
  | .oldOriginal copyTo h => moving "old_original" copyTo h
  | .nonmovingOriginal marked => .mkObj [("kind", .str "nonmoving_original"), ("marked", .bool marked)]
  | .ordinaryDestination => .mkObj [("kind", .str "ordinary_destination")]
  | .nonmovingDestination => .mkObj [("kind", .str "nonmoving_destination")]
where
  moving (kind : String) (copyTo : CopyTo) (h : Header) : Lean.Json :=
    .mkObj [("kind", .str kind), ("copy_to", .str (match copyTo with | .ordinary => "ordinary" | .nonmoving => "nonmoving")),
      ("header", headerJson h)]

/-- Validate first and preserve every root/slot/unit order and cell tag. -/
def writeHeap (heap : HeapSnapshot) : Except String String := do
  let heap := (← validateAndOrder heap).val
  let json := Lean.Json.mkObj [("format", .str "clover-gc-heap"), ("version", natJson 2),
    ("roots", .arr (heap.roots.map cellJson)),
    ("objects", .arr (heap.objects.map fun a => .mkObj [("object_id", .str a.objectId.value),
      ("allocation_id", natJson a.allocationId.value), ("state", stateJson a.state),
      ("strong", .arr (a.strong.map cellJson)), ("weak", unitsJson a.weak)])),
    ("external_weak", unitsJson heap.externalWeak),
    ("remembered", .arr (heap.remembered.map fun id => natJson id.value))]
  return json.compress ++ "\n"

end CloverGC.Differential.Format
