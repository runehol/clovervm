import CloverGC.Differential.HeapFormat

open CloverGC.Differential
open CloverGC.Differential.Format

private def ok (label : String) (result : Except String α) : IO α :=
  match result with
  | .ok value => pure value
  | .error message => throw (IO.userError s!"{label}: {message}")
private def isRejected (result : Except String α) : Bool := !result.isOk
private def fixture (name : String) : IO ByteArray :=
  IO.FS.readBinFile s!"../../tests/gc_differential/fixtures/{name}.json"
private def changeObject (heap : HeapSnapshot) (id : Nat) (f : Allocation → Allocation) : HeapSnapshot :=
  { heap with objects := heap.objects.map fun a => if a.allocationId.value == id then f a else a }
private def emptyText : String :=
  "{\"format\":\"clover-gc-heap\",\"version\":2,\"roots\":[],\"objects\":[],\"external_weak\":[],\"remembered\":[]}"
private def ref (n : Nat) : Cell := .reference (some ⟨n⟩)

-- Runtime assertions deliberately stay outside collector proofs and their
-- audited dependency graph. Run from proofs/gc: lake exe gc_format_tests.
def main : IO Unit := do
  let checks ← IO.mkRef 0
  let check := fun (label : String) (condition : Bool) => do
    checks.modify (· + 1)
    unless condition do throw (IO.userError s!"failed: {label}")
  let reject := fun (label text : String) => check label (isRejected (parseHeap text.toUTF8))
  let rejectHeap := fun (label : String) (heap : HeapSnapshot) => do
    check (label ++ ": validator") (isRejected (validateHeap heap))
    check (label ++ ": writer") (isRejected (writeHeap heap))
  let mut snapshots := #[]
  for name in ["before", "after", "after_mark"] do
    let bytes ← fixture name
    let heap ← ok name (parseHeap bytes)
    let output ← ok (name ++ " writer") (writeHeap heap)
    let roundTrip ← ok (name ++ " round trip") (parseHeap output.toUTF8)
    check (name ++ " round trip") (roundTrip == heap)
    check (name ++ " deterministic") ((← ok "positive result" (writeHeap roundTrip)) == output)
    check (name ++ " fresh classification") ((validateFreshHeap heap).isOk == (name == "before"))
    snapshots := snapshots.push heap
  let some before := snapshots[0]? | throw (IO.userError "missing before fixture")
  let some after := snapshots[1]? | throw (IO.userError "missing after fixture")
  rejectHeap "missing remembered coverage" { before with remembered := #[] }
  rejectHeap "duplicate remembered owner" { before with remembered := #[⟨0⟩, ⟨0⟩] }
  rejectHeap "young remembered owner" { before with remembered := #[⟨0⟩, ⟨1⟩] }
  rejectHeap "unknown remembered owner" { before with remembered := #[⟨0⟩, ⟨99⟩] }
  check "conservative remembered owner" (validateHeap { before with remembered := #[⟨0⟩, ⟨2⟩] }).isOk
  let weakOwner ← ok "weak-only fixture" (parseHeap (← IO.FS.readBinFile "../../tests/gc_differential/fixtures/remembered_weak_before.json"))
  rejectHeap "weak-only missing remembered owner" { weakOwner with remembered := #[] }
  let inactiveHeap := { changeObject weakOwner 0 (fun a =>
      { a with weak := #[⟨⟨0⟩, .entry ⟨none, some ⟨0⟩, ref 2⟩⟩] }) with
    externalWeak := #[⟨⟨1⟩, .field (.immediate 0)⟩], remembered := #[] }
  rejectHeap "inactive weak payload needs remembered owner" inactiveHeap
  let [a, b, c] := before.objects.toList | throw (IO.userError "wrong original count")
  check "decoded roots" (before.roots == #[ref 1, ref 1])
  check "decoded identities" (a.objectId == ⟨"obj-0"⟩ && b.objectId == ⟨"obj-1"⟩ && c.objectId == ⟨"obj-2"⟩)
  check "decoded original states" (a.state == .nonmovingOriginal false &&
    b.state == .youngOriginal .ordinary (.normal false) && c.state == .oldOriginal .nonmoving (.normal false))
  check "decoded strong slots" (b.strong == #[ref 0, .immediate 7])
  check "decoded owner weak" (b.weak == #[⟨⟨0⟩, .field (.immediate (-7))⟩])
  check "decoded external entry" (before.externalWeak == #[⟨⟨1⟩, .entry ⟨some ⟨1⟩, some ⟨0⟩, ref 2⟩⟩])
  check "decoded repaired external entry" (after.externalWeak == #[⟨⟨1⟩, .entry ⟨none, none, .immediate 0⟩⟩])
  check "decoded copy" (after.objects[3]?.map (fun a => (a.objectId, a.state)) ==
    some (⟨"obj-1"⟩, .ordinaryDestination))
  check "decoded forwarding" (after.objects[1]?.map Allocation.state ==
    some (.youngOriginal .ordinary (.forwarded ⟨3⟩)))

  let json ← ok "fixture JSON" (Lean.Json.parse (String.fromUTF8! (← fixture "before")))
  let object ← ok "fixture object" json.getObj?
  let objects ← ok "fixture objects" (← ok "fixture objects field" (json.getObjVal? "objects")).getArr?
  let unordered := Lean.Json.mkObj (object.toList.map fun (k, v) =>
    (k, if k == "objects" then .arr objects.reverse else v))
  check "unordered input normalizes records" ((← ok "positive result" (parseHeap unordered.compress.toUTF8)) == before)
  let permuted := changeObject (changeObject before 0 (fun a => { a with objectId := ⟨"obj-2"⟩ }))
    2 (fun a => { a with objectId := ⟨"obj-0"⟩ })
  check "independent logical/allocation pairing" ((← ok "positive result" (parseHeap (← ok "permuted writer" (writeHeap permuted)).toUTF8)) == permuted)
  let stable := changeObject (changeObject after 1 (fun a =>
    { a with state := .oldOriginal .nonmoving (.forwarded ⟨3⟩) })) 3 (fun a => { a with state := .nonmovingDestination })
  check "old forwarding and nonmoving destination" ((← ok "positive result" (parseHeap (← ok "stable writer" (writeHeap stable)).toUTF8)) == stable)

  let empty ← ok "empty parser" (parseHeap emptyText.toUTF8)
  check "empty fresh" (validateFreshHeap empty).isOk
  let extremes := { empty with roots := #[.immediate (-9223372036854775808),
    .immediate 9223372036854775807, .immediate 0, .reference none] }
  check "integer extremes and null tags" ((← ok "positive result" (parseHeap (← ok "extreme writer" (writeHeap extremes)).toUTF8)) == extremes)
  let rawExtremes := emptyText.replace "\"roots\":[]"
    "\"roots\":[{\"imm\":-9223372036854775808},{\"imm\":9223372036854775807},{\"imm\":0},{\"ref\":null}]"
  check "decoded integer extremes" ((← ok "positive result" (parseHeap rawExtremes.toUTF8)) == extremes)
  let escapedKey := emptyText.replace "\"version\"" "\"ver\\u0073ion\""
  check "escaped ASCII key" ((← ok "escaped key" (parseHeap escapedKey.toUTF8)) == empty)
  let emptyUnits := { empty with externalWeak := #[⟨⟨0⟩, .field (.reference none)⟩,
    ⟨⟨1⟩, .entry ⟨none, none, .immediate 42⟩⟩] }
  check "units without objects" ((← ok "empty units" (parseHeap
    (← ok "empty units writer" (writeHeap emptyUnits)).toUTF8)) == emptyUnits)
  for state in [ObjectState.youngOriginal .ordinary (.normal true),
      .oldOriginal .ordinary (.normal true)] do
    let marked := changeObject before 1 (fun a => { a with state := state })
    check "marked movable snapshot is structurally valid" (validateHeap marked).isOk
    check "marked movable original is not fresh" (isRejected (validateFreshHeap marked))
  let mixed := { before with
    roots := #[ref 1, .immediate (-7), .reference none, ref 1]
    externalWeak := before.externalWeak ++ #[⟨⟨2⟩, .field (.reference none)⟩,
      ⟨⟨3⟩, .entry ⟨none, some ⟨1⟩, .immediate 42⟩⟩] }
  check "ordered mixed external units and inactive entry" ((← ok "positive result" (parseHeap (← ok "mixed writer" (writeHeap mixed)).toUTF8)) == mixed)
  check "inactive values are valid fresh data" (validateFreshHeap mixed).isOk

  reject "malformed JSON" "{broken"
  reject "missing required field" (emptyText.replace "\"version\":2," "")
  reject "unsupported version" (emptyText.replace "\"version\":2" "\"version\":3")
  for value in ["{\"imm\":-9223372036854775809}", "{\"imm\":9223372036854775808}",
      "{\"imm\":true}", "{\"ref\":-1}", "{\"ref\":true}", "{\"ref\":\"obj-0\"}"] do
    reject "invalid cell value" (emptyText.replace "\"roots\":[]" s!"\"roots\":[{value}]")
  for id in ["obj-00", "obj-", "obj--1", "obj-+1", "obj- 1", "obj-1x", "obj-18446744073709551616", "other-0"] do
    rejectHeap ("object identity " ++ id) (changeObject before 0 (fun a => { a with objectId := ⟨id⟩ }))
  for (label, bad) in [
      ("allocation gap", changeObject before 0 (fun a => { a with allocationId := ⟨3⟩ })),
      ("duplicate allocation", changeObject before 1 (fun a => { a with allocationId := ⟨0⟩ })),
      ("logical gap", changeObject before 0 (fun a => { a with objectId := ⟨"obj-3"⟩ })),
      ("duplicate original", changeObject before 0 (fun a => { a with objectId := ⟨"obj-1"⟩ })),
      ("unknown root", { before with roots := #[ref 3] }),
      ("unknown strong reference", changeObject before 1 (fun a => { a with strong := #[ref 3] })),
      ("unknown field reference", changeObject before 1 (fun a => { a with weak := #[⟨⟨0⟩, .field (ref 3)⟩] })),
      ("unknown guard", { before with externalWeak := #[⟨⟨1⟩, .entry ⟨some ⟨3⟩, some ⟨0⟩, ref 2⟩⟩] }),
      ("unknown payload", { before with externalWeak := #[⟨⟨1⟩, .entry ⟨some ⟨1⟩, some ⟨0⟩, ref 3⟩⟩] }),
      ("duplicate unit", { before with externalWeak := #[⟨⟨0⟩, .field (.immediate 0)⟩] }),
      ("unit gap", { before with externalWeak := #[⟨⟨2⟩, .field (.immediate 0)⟩] }),
      ("owner order", changeObject before 1 (fun a => { a with weak := #[
        ⟨⟨0⟩, .entry ⟨none, none, .immediate 0⟩⟩, ⟨⟨2⟩, .field (.immediate 0)⟩] })),
      ("orphan destination", changeObject after 1 (fun a => { a with state := .youngOriginal .ordinary (.normal false) })),
      ("wrong forward target", changeObject after 1 (fun a => { a with state := .youngOriginal .ordinary (.forwarded ⟨0⟩) })),
      ("wrong destination identity", changeObject after 3 (fun a => { a with objectId := ⟨"obj-2"⟩ })),
      ("wrong destination placement", changeObject after 3 (fun a => { a with state := .nonmovingDestination })),
      ("wrong weak layout size", changeObject after 3 (fun a => { a with weak := #[] })),
      ("wrong weak unit identity", changeObject after 3 (fun a => { a with weak := #[⟨⟨1⟩, .field (.immediate 0)⟩] })),
      ("wrong weak kind", changeObject after 3 (fun a => { a with weak := #[⟨⟨0⟩, .entry ⟨none, none, .immediate 0⟩⟩] })),
      ("nonmoving original with copy", changeObject after 1 (fun a => { a with state := .nonmovingOriginal false })),
      ("typed immediate overflow", { empty with roots := #[.immediate 9223372036854775808] }),
      ("typed negative overflow", { empty with roots := #[.immediate (-9223372036854775809)] })] do
    rejectHeap label bad

  let beforeText := String.fromUTF8! (← fixture "before")
  for (old, replacement) in [
      ("young_original", "unknown"), ("ordinary", "unknown"), ("normal", "unknown"),
      ("\"marked\": false", "\"marked\": 0"),
      ("\"kind\": \"field\"", "\"kind\": \"unknown\""),
      ("\"guards\": [\n        1,\n        0\n      ]", "\"guards\": [1]"),
      ("\"guards\": [\n        1,\n        0\n      ]", "\"guards\": [1,0,1]"),
      ("\"guards\": [\n        1,\n        0\n      ]", "\"guards\": [true,0]") ] do
    check "malformed fixture replacement is exercised" (beforeText.contains old)
    reject "malformed state/unit" (beforeText.replace old replacement)
  check "escaped ASCII object identity" ((← ok "escaped ID" (parseHeap
    (beforeText.replace "obj-0" "obj-\\u0030").toUTF8)) == before)
  IO.println s!"Lean GC format tests passed ({← checks.get} checks)."
