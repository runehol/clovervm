import Lean.Data.Json

private def unwrap (result : Except String α) : IO α :=
  match result with
  | .ok value => pure value
  | .error message => throw (IO.userError message)
private def require (label : String) (condition : Bool) : IO Unit :=
  unless condition do throw (IO.userError label)

-- Run from proofs/gc. The executable dependency also builds the tested CLI.
def main : IO Unit := IO.FS.withTempDir fun temp => do
  let runner := (← IO.appPath).withFileName "gc_lean"
  let fixtures : System.FilePath := "../../tests/gc_differential/fixtures"
  let before := (fixtures / "before.json").toString
  let cases ← IO.mkRef 0
  let run := fun args => do
    cases.modify (· + 1)
    IO.Process.output { cmd := runner.toString, args := args }
  let success := fun (label : String) (expected : System.FilePath) args => do
    let output ← run args
    require (label ++ ": exit status or stderr") (output.exitCode == 0 && output.stderr.isEmpty)
    let actual ← unwrap (Lean.Json.parse output.stdout)
    let expected ← unwrap (Lean.Json.parse (← IO.FS.readFile expected))
    require (label ++ ": heap mismatch") (actual == expected)
  let failure := fun (label diagnostic : String) args => do
    let output ← run args
    require (label ++ ": exit status") (output.exitCode == 1)
    require (label ++ ": stdout on failure") output.stdout.isEmpty
    require (label ++ ": missing diagnostic")
      (output.stderr.startsWith "gc_lean:" && output.stderr.contains diagnostic)

  success "mark" (fixtures / "after_mark.json") #["--algorithm", "mark", "--collection", "full", before]
  success "evacuate" (fixtures / "after.json") #["--algorithm", "evacuate", "--collection", "full", before]
  success "young" (fixtures / "young_after.json")
    #["--algorithm", "evacuate", "--collection", "young", (fixtures / "young_before.json").toString]
  success "full with remembered owners" (fixtures / "young_full_after.json")
    #["--algorithm", "evacuate", "--collection", "full", (fixtures / "young_before.json").toString]
  let spaced := temp / "input heap, with spaces.json"
  IO.FS.writeBinFile spaced (← IO.FS.readBinFile (fixtures / "before.json"))
  success "equals, reordered flags and exact path" (fixtures / "after.json")
    #["--collection=full", "--algorithm=evacuate", "--", spaced.toString]
  for (name, args) in [("--algorithm=mark", #["--algorithm", "mark", "--collection", "full", "--", "--algorithm=mark"]),
      ("-", #["--algorithm", "mark", "--collection", "full", "-"])] do
    IO.FS.writeBinFile (temp / name) (← IO.FS.readBinFile (fixtures / "before.json"))
    let output ← IO.Process.output {
      cmd := runner.toString
      cwd := some temp
      args := args }
    cases.modify (· + 1)
    require ("literal path " ++ name) (output.exitCode == 0 && output.stderr.isEmpty &&
      (← unwrap (Lean.Json.parse output.stdout)) == (← unwrap (Lean.Json.parse
        (← IO.FS.readFile (fixtures / "after_mark.json")))))
  let empty := temp / "empty.json"
  IO.FS.writeFile empty "{\"format\":\"clover-gc-heap\",\"version\":2,\"roots\":[],\"objects\":[],\"external_weak\":[],\"remembered\":[]}"
  for algorithm in ["mark", "evacuate"] do
    success ("empty " ++ algorithm) empty #["--algorithm", algorithm, "--collection", "full", empty.toString]
  for args in [#["--help"], #["-h"], #["--algorithm", "unknown", "--help"],
      #["--collection", "young", "--help"], #["--help", "--algorithm", "mark", "--algorithm", "evacuate"]] do
    let output ← run args
    require "help" (output.exitCode == 0 && output.stderr.isEmpty &&
      output.stdout.contains "--algorithm" && output.stdout.contains "FILE")
  failure "unknown option after help" "unknown-option" #["--help", "--unknown-option"]
  failure "missing option value after help" "missing value" #["--help", "--algorithm"]
  failure "no arguments" "--algorithm" #[]
  failure "missing algorithm" "--algorithm" #["--collection", "full", before]
  failure "missing collection" "--collection" #["--algorithm", "mark", before]
  failure "missing file" "one input heap file" #["--algorithm", "mark", "--collection", "full"]
  failure "extra file" "one input heap file" #["--algorithm", "mark", "--collection", "full", before, before]
  failure "unsupported collection" "unsupported collection" #["--algorithm", "mark", "--collection", "unknown", before]
  failure "young mark" "evacuation only" #["--algorithm", "mark", "--collection", "young", before]
  success "young weak" (fixtures / "weak_young_after.json") #["--algorithm", "evacuate", "--collection", "young", before]
  for (collection, expected) in [("young", "remembered_weak_after.json"), ("full", "remembered_weak_full_after.json")] do
    success ("remembered weak owner " ++ collection) (fixtures / expected)
      #["--algorithm", "evacuate", "--collection", collection, (fixtures / "remembered_weak_before.json").toString]
  failure "unsupported algorithm" "unsupported algorithm" #["--algorithm", "unknown", "--collection", "full", before]
  failure "unknown option" "unknown-option" #["--algorithm", "mark", "--collection", "full", "--unknown-option", before]
  for option in ["--algorithm", "--collection"] do
    failure "missing option value" "missing value" #[option]
  for algorithm in ["mark", "evacuate"] do
    failure "duplicate algorithm" "exactly one --algorithm"
      #["--algorithm", "mark", "--algorithm", algorithm, "--collection", "full", before]
  for selection in ["full", "young"] do
    failure "duplicate collection" "exactly one --collection"
      #["--algorithm", "mark", "--collection", "full", "--collection", selection, before]
  failure "unreadable file" "does-not-exist.json"
    #["--algorithm", "mark", "--collection", "full", (temp / "does-not-exist.json").toString]
  failure "directory" "cannot read input file" #["--algorithm", "mark", "--collection", "full", temp.toString]
  let malformed := temp / "malformed.json"
  IO.FS.writeFile malformed "{broken"
  failure "malformed JSON" "gc_lean:" #["--algorithm", "mark", "--collection", "full", malformed.toString]
  IO.FS.writeBinFile malformed ⟨#[255]⟩
  failure "invalid UTF-8" "UTF-8" #["--algorithm", "mark", "--collection", "full", malformed.toString]
  let invalid := temp / "invalid.json"
  IO.FS.writeFile invalid "{\"format\":\"clover-gc-heap\",\"version\":2,\"roots\":[{\"ref\":0}],\"objects\":[],\"external_weak\":[],\"remembered\":[]}"
  failure "invalid reference" "reference" #["--algorithm", "evacuate", "--collection", "full", invalid.toString]
  for algorithm in ["mark", "evacuate"] do
    failure "completed heap" "fresh heap" #["--algorithm", algorithm, "--collection", "full", (fixtures / "after.json").toString]
  IO.println s!"Lean GC CLI tests passed ({← cases.get} process cases)."
