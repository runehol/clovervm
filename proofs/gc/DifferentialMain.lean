import CloverGC.Differential.HeapRunner

open CloverGC.Differential

private inductive Command where
  | help
  | collect (algorithm : Algorithm) (file : System.FilePath)

private structure Arguments where
  algorithms : List String := []
  collections : List String := []
  help : Bool := false
  files : List String := []

private def Arguments.finish (args : Arguments) : Except String Command := do
  if args.help then return .help
  let [algorithm] := args.algorithms | throw "specify exactly one --algorithm mark|evacuate"
  let [collection] := args.collections | throw "specify exactly one --collection full"
  unless collection == "full" do throw "unsupported collection; only full is supported"
  let algorithm ← match algorithm with
    | "mark" => pure Algorithm.mark
    | "evacuate" => pure Algorithm.evacuate
    | _ => throw "unsupported algorithm; use mark or evacuate"
  let [file] := args.files | throw "expected exactly one input heap file"
  return .collect algorithm file

private def parseArgs : List String → Arguments → Except String Command
  | [], args => args.finish
  | "--help" :: rest, args | "-h" :: rest, args => parseArgs rest { args with help := true }
  | "--" :: rest, args => { args with files := rest ++ args.files }.finish
  | "--algorithm" :: value :: rest, args =>
      parseArgs rest { args with algorithms := value :: args.algorithms }
  | "--collection" :: value :: rest, args =>
      parseArgs rest { args with collections := value :: args.collections }
  | ["--algorithm"], _ => throw "missing value for --algorithm"
  | ["--collection"], _ => throw "missing value for --collection"
  | arg :: rest, args => do
      if arg.startsWith "--algorithm=" then
        parseArgs rest { args with algorithms := String.ofList (arg.toList.drop 12) :: args.algorithms }
      else if arg.startsWith "--collection=" then
        parseArgs rest { args with collections := String.ofList (arg.toList.drop 13) :: args.collections }
      else if arg != "-" && arg.startsWith "-" then throw s!"unknown option: {arg}"
      else parseArgs rest { args with files := arg :: args.files }

private def unwrap (result : Except String α) : IO α :=
  match result with
  | .ok value => pure value
  | .error message => throw (IO.userError message)

def main (args : List String) : IO UInt32 := do
  try
    let output ← match ← unwrap (parseArgs args {}) with
      | .help => pure "Usage: gc_lean --algorithm mark|evacuate --collection full FILE\n"
      | .collect algorithm file => do
          if ← file.isDir then throw (IO.userError s!"cannot read input file: {file}")
          let heap ← unwrap (Format.parseHeap (← IO.FS.readBinFile file))
          let result ← unwrap (runCollection heap algorithm)
          unwrap (Format.writeHeap result)
    -- Finish validation and serialization before emitting the heap.
    let stdout ← IO.getStdout
    stdout.putStr output
    stdout.flush
    return 0
  catch error =>
    let stderr ← IO.getStderr
    stderr.putStrLn s!"gc_lean: {error}"
    stderr.flush
    return 1
