import CloverGC.Generational
import CloverGC.WeakRepair

namespace CloverGC.Generational.Weak

open Copying Copying.Weak

/-- Selection precedes forwarding and marks, as in C++ resolve_weak. Weak
    resolution never discovers objects. Copied references must be allocated;
    the strong invariant guarantees that every allocated copy is marked. -/
def resolve (input : CollectionInput n) (copying : CopyState n) (phase : Phase) :
    Reference n → Resolution n
  | .source object =>
      if input.selected object then
        Copying.Weak.resolve input.copyInput copying phase (.source object)
      else .resolved (.source object)
  | .copied index => .resolved (.copied index)

end CloverGC.Generational.Weak
