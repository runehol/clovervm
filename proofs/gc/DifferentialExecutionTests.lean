import CloverGC.Differential.HeapRunner

open CloverGC CloverGC.Differential CloverGC.Differential.Format

private def ok (label : String) (result : Except String α) : IO α :=
  match result with
  | .ok value => pure value
  | .error message => throw (IO.userError s!"{label}: {message}")
private def fixture (name : String) : IO HeapSnapshot := do
  ok name (parseHeap (← IO.FS.readBinFile s!"../../tests/gc_differential/fixtures/{name}.json"))
private def ref (n : Nat) : Cell := .reference (some ⟨n⟩)
private def field (id : Nat) (value : Cell) : WeakUnit := ⟨⟨id⟩, .field value⟩
private def entry (id : Nat) (a b : Option Nat) (value : Cell) : WeakUnit :=
  ⟨⟨id⟩, .entry ⟨a.map AllocationId.mk, b.map AllocationId.mk, value⟩⟩
private def reset (id : Nat) : WeakUnit := entry id none none (.immediate 0)
private def mixedHeap (first second : CopyTo) : HeapSnapshot :=
  ⟨#[ref 3, .immediate (-5), ref 3, .reference none], #[
    ⟨⟨"obj-2"⟩, ⟨0⟩, .oldOriginal second (.normal false), #[ref 3, ref 0], #[field 7 (ref 2)]⟩,
    ⟨⟨"obj-0"⟩, ⟨1⟩, .nonmovingOriginal false, #[ref 3],
      #[field 5 (ref 3), entry 6 (some 3) (some 0) (.immediate 0)]⟩,
    ⟨⟨"obj-3"⟩, ⟨2⟩, .oldOriginal .ordinary (.normal false), #[ref 1],
      #[field 8 (ref 0), entry 9 none (some 3) (ref 0)]⟩,
    ⟨⟨"obj-1"⟩, ⟨3⟩, .youngOriginal first (.normal false), #[ref 1, .immediate 7, ref 0, .reference none],
      #[field 2 (.immediate (-7)), field 3 (ref 0), entry 4 (some 3) (some 1) (ref 2),
        entry 10 none (some 2) (ref 0)]⟩],
    #[entry 0 (some 3) (some 1) (ref 0), field 1 (.immediate 0)], #[⟨0⟩, ⟨1⟩, ⟨2⟩]⟩

-- These are runtime assertions, outside the collector proofs' dependency graph.
def main : IO Unit := do
  let checks ← IO.mkRef 0
  let check := fun (label : String) (condition : Bool) => do
    checks.modify (· + 1)
    unless condition do throw (IO.userError s!"failed: {label}")
  let before ← fixture "before"
  for (algorithm, name) in [(Algorithm.mark, "after_mark"), (.evacuate, "after")] do
    check name ((← ok name (runCollection before algorithm)) == (← fixture name))
  let young ← fixture "young_before"
  let youngAfter ← fixture "young_after"
  check "young remembered field repair" ((← ok "young" (runCollection young .evacuate .young)) == youngAfter)
  check "full ignores remembered roots" ((← ok "full" (runCollection young .evacuate)) == (← fixture "young_full_after"))
  let floating := { young with roots := #[] }
  check "full does not retain through garbage remembered owners" ((← ok "full floating" (runCollection floating .evacuate)) == floating)
  check "minor retains through unrooted old owners" ((← ok "young floating" (runCollection floating .evacuate .young)).objects == youngAfter.objects)
  check "young mark unsupported" (!(runCollection young .mark .young).isOk)
  check "young weak" ((← ok "young weak" (runCollection before .evacuate .young)) == (← fixture "weak_young_after"))
  let weakOwner ← fixture "remembered_weak_before"
  check "weak-only remembered owner" ((← ok "young weak owner" (runCollection weakOwner .evacuate .young)) == (← fixture "remembered_weak_after"))
  check "full ignores weak-only remembered owner" ((← ok "full weak owner" (runCollection weakOwner .evacuate .full)) == (← fixture "remembered_weak_full_after"))
  let ⟨_, _, prepared⟩ ← ok "young prepare" (prepareHeap young)
  check "proof-carrying remembered logical pairing"
    (prepared.generational.data.rememberedOwners.map Fin.val == [4, 3, 2])
  let empty : HeapSnapshot := ⟨#[.immediate (-9223372036854775808), .reference none,
    .immediate 9223372036854775807], #[], #[], #[]⟩
  for algorithm in [Algorithm.mark, .evacuate] do
    check "empty and immediate roots" ((← ok "empty" (runCollection empty algorithm)) == empty)
  let oldOnly : HeapSnapshot := { empty with roots := #[], objects := #[
    ⟨⟨"obj-0"⟩, ⟨1⟩, .nonmovingOriginal false, #[], #[]⟩,
    ⟨⟨"obj-1"⟩, ⟨0⟩, .oldOriginal .nonmoving (.normal false), #[], #[]⟩] }
  let oldOnlyExpected := { oldOnly with objects := oldOnly.objects.reverse }
  check "young retains unreachable old originals with permuted IDs"
    ((← ok "old-only young" (runCollection oldOnly .evacuate .young)) == oldOnlyExpected)
  let external := { empty with externalWeak := #[⟨⟨0⟩, .field (.immediate 17)⟩,
    ⟨⟨1⟩, .entry ⟨none, none, .immediate 42⟩⟩] }
  let repaired := { external with externalWeak := #[⟨⟨0⟩, .field (.immediate 17)⟩,
    ⟨⟨1⟩, .entry ⟨none, none, .immediate 0⟩⟩] }
  check "external units without objects" ((← ok "external" (runCollection external .evacuate)) == repaired)
  let unrooted := { before with roots := #[] }
  check "mark with no roots" ((← ok "unrooted" (runCollection unrooted .mark)) == unrooted)
  let unrootedResult ← ok "unrooted evacuation" (runCollection unrooted .evacuate)
  check "no spurious copies" (unrootedResult.objects == before.objects)
  check "dead external entry resets" (unrootedResult.externalWeak == repaired.externalWeak.extract 1 2)

  for (first, second) in [(CopyTo.ordinary, CopyTo.nonmoving), (.nonmoving, .ordinary)] do
    let input := mixedHeap first second
    let destination := fun policy => match policy with
      | CopyTo.ordinary => ObjectState.ordinaryDestination
      | .nonmoving => .nonmovingDestination
    let expected : HeapSnapshot := ⟨#[ref 4, .immediate (-5), ref 4, .reference none], #[
      ⟨⟨"obj-2"⟩, ⟨0⟩, .oldOriginal second (.forwarded ⟨5⟩), #[ref 3, ref 0], #[field 7 (ref 2)]⟩,
      ⟨⟨"obj-0"⟩, ⟨1⟩, .nonmovingOriginal true, #[ref 4],
        #[field 5 (ref 4), entry 6 (some 4) (some 5) (.immediate 0)]⟩,
      ⟨⟨"obj-3"⟩, ⟨2⟩, .oldOriginal .ordinary (.normal false), #[ref 1],
        #[field 8 (ref 0), entry 9 none (some 3) (ref 0)]⟩,
      ⟨⟨"obj-1"⟩, ⟨3⟩, .youngOriginal first (.forwarded ⟨4⟩), #[ref 1, .immediate 7, ref 0, .reference none],
        #[field 2 (.immediate (-7)), field 3 (ref 0), entry 4 (some 3) (some 1) (ref 2),
          entry 10 none (some 2) (ref 0)]⟩,
      ⟨⟨"obj-1"⟩, ⟨4⟩, destination first, #[ref 1, .immediate 7, ref 5, .reference none],
        #[field 2 (.immediate (-7)), field 3 (ref 5), reset 4, reset 10]⟩,
      ⟨⟨"obj-2"⟩, ⟨5⟩, destination second, #[ref 4, ref 5], #[field 7 (.reference none)]⟩],
      #[entry 0 (some 4) (some 1) (ref 5), field 1 (.immediate 0)], #[⟨0⟩, ⟨1⟩, ⟨2⟩]⟩
    check "mixed destinations and weak repair" ((← ok "mixed" (runCollection input .evacuate)) == expected)
    let ⟨_, _, mixed⟩ ← ok "mixed prepare" (prepareHeap input)
    let finished := Copying.Weak.collectWeak mixed.copying mixed.weak mixed.roots
    let some nonmoving := mixed.objectOfAllocation.toList[1]? | throw (IO.userError "missing nonmoving object")
    let copying := finished.strong.copying
    check "nonmoving forwarding rejected" (!(captureEvacuated mixed { finished with
      strong := { finished.strong with copying := { copying with
        forwarding := fun object => if object == nonmoving then some 0 else copying.forwarding object } } }).isOk)

  -- Logical identities are permuted independently of physical allocation IDs.
  let permuted := { before with objects := before.objects.map fun a =>
    { a with objectId := ⟨if a.objectId.value == "obj-0" then "obj-2"
      else if a.objectId.value == "obj-2" then "obj-0" else "obj-1"⟩ } }
  let result ← ok "permuted" (runCollection permuted .evacuate)
  check "permuted record order" ((← ok "unordered" (runCollection
    { permuted with objects := permuted.objects.reverse } .evacuate)) == result)
  let ⟨_, _, heap⟩ ← ok "prepare" (prepareHeap permuted)
  check "physical to logical mapping" (heap.objectOfAllocation.toArray.map Fin.val == #[2, 1, 0])
  check "roots retain duplicates" (heap.roots.map Fin.val == [1, 1])
  let state := Copying.Weak.collectWeak heap.copying heap.weak heap.roots
  let reject := fun label state => check label (!(captureEvacuated heap state).isOk)
  reject "unfinished strong queue" { state with strong := { state.strong with
    copying := { state.strong.copying with pending := [.copied 0] } } }
  reject "unfinished weak queue" { state with pending := heap.weak.external }
  reject "missing root" { state with strong := { state.strong with roots := [] } }
  reject "extra root" { state with strong := { state.strong with roots := state.strong.roots ++ [.copied 0] } }
  reject "unallocated reference" { state with strong := { state.strong with roots := [.copied 100, .copied 0] } }
  reject "extra original field" { state with strong := { state.strong with
    originalFields := fun object => state.strong.originalFields object ++ [.copied 0] } }
  reject "missing original field" { state with strong := { state.strong with
    originalFields := fun _ => [] } }
  reject "missing copy mark" { state with strong := { state.strong with
    copying := { state.strong.copying with marked := [] } } }
  reject "forwarded source marked" { state with strong := { state.strong with
    copying := { state.strong.copying with
      marked := heap.roots.map Copying.Reference.source ++ state.strong.copying.marked } } }
  reject "wrong copy placement" { state with strong := { state.strong with
    copying := { state.strong.copying with copies := state.strong.copying.copies.map fun r =>
      { r with destination := .nonMoving } } } }
  let some other := heap.objectOfAllocation.toList.head? | throw (IO.userError "missing object")
  reject "wrong copy origin" { state with strong := { state.strong with
    copying := { state.strong.copying with copies := state.strong.copying.copies.map fun r =>
      { r with origin := other } } } }
  reject "extra copied field" { state with strong := { state.strong with
    copying := { state.strong.copying with copies := state.strong.copying.copies.map fun r =>
      { r with strongChildren := r.strongChildren ++ [.copied 0] } } } }
  reject "missing copied field" { state with strong := { state.strong with
    copying := { state.strong.copying with copies := state.strong.copying.copies.map fun r =>
      { r with strongChildren := [] } } } }
  reject "changed weak kind" { state with units := fun _ => .field (.immediate 0) }
  let badUnits := fun id => match state.units id with
    | .field _ => .field (.reference (some (.copied 100)))
    | .entry e => .entry e
  reject "unallocated weak reference" { state with units := badUnits }
  let wrongSource := { state with strong := { state.strong with
    roots := heap.roots.map Copying.Reference.source } }
  let observed ← ok "capture stale source" (captureEvacuated heap wrongSource)
  check "capture does not normalize stale references" (observed.roots == before.roots && observed.roots != result.roots)
  let marked := (collect heap.copying.heap heap.roots).1
  check "incomplete mark rejected" (!(captureMark heap { marked with pending := heap.roots }).isOk)

  -- More copies than one native arena chunk; one global ordinal across policies.
  let count := 140
  let chain : HeapSnapshot := ⟨#[ref 0, .immediate 7, .reference none, ref 0],
    (List.range count).toArray.map (fun i => ⟨⟨s!"obj-{count - 1 - i}"⟩, ⟨i⟩,
      .oldOriginal (if i % 2 == 0 then .ordinary else .nonmoving) (.normal false),
      #[.immediate (-7), ref ((i + 1) % count), .reference none],
      #[⟨⟨i⟩, .field (ref ((i + 1) % count))⟩]⟩), #[], #[]⟩
  let copied ← ok "long cycle" (runCollection chain .evacuate)
  check "all copies retained" (copied.objects.size == 2 * count)
  check "root slots retained" (copied.roots == #[ref count, .immediate 7, .reference none, ref count])
  for i in List.range count do
    let some original := copied.objects[i]? | throw (IO.userError "missing original")
    let some destination := copied.objects[count + i]? | throw (IO.userError "missing destination")
    check "source keeps fields and weak storage" (original.strong == #[.immediate (-7), ref ((i + 1) % count), .reference none] &&
      original.weak == #[⟨⟨i⟩, .field (ref ((i + 1) % count))⟩])
    check "global copy IDs and identity" (destination.allocationId.value == count + i && destination.objectId == original.objectId)
    check "copy placement" (destination.state == if i % 2 == 0 then .ordinaryDestination else .nonmovingDestination)
    check "copy slot reconstruction" (destination.strong == #[.immediate (-7), ref (count + (i + 1) % count), .reference none] &&
      destination.weak == #[⟨⟨i⟩, .field (ref (count + (i + 1) % count))⟩])
  IO.println s!"Lean GC execution tests passed ({← checks.get} checks)."
