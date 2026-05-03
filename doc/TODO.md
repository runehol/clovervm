# clovervm TODO

This file tracks near-term work that still matters after the unified object
model and first attribute inline-cache slices.

## Top Priority

- [ ] Expand constructor semantics beyond tier-1 constructor thunks.

  `Cls(...)` now uses an on-demand constructor thunk for ordinary classes:
  standard `type`, no custom `__new__`, absent or plain-function `__init__`,
  positional arguments, defaults, varargs, and non-`None` `__init__` return
  checks are covered by interpreter tests.

  Remaining construction work:

  1. add outer `CallKw` support so keyword constructor calls can still reuse
     the prepared thunk body after call-site adaptation
  2. implement the generic path for custom `__new__`
  3. implement or explicitly stage custom metaclass `__call__`
  4. normalize constructor failures into specific VM exceptions

- [ ] Execute descriptor protocol calls through interpreter-controlled dispatch.

  Lookup now classifies descriptors and surfaces descriptor get/set/delete work
  as descriptor plans. The next semantic step is to actually call descriptor
  `__get__`, `__set__`, and `__delete__` without hiding Python bytecode
  execution inside lookup helpers. This is intentionally parked behind a real
  VM exception story because descriptor calls need to report and propagate
  Python-visible failures correctly.

- [ ] Continue tightening attribute inline-cache hot paths.

  `LoadAttr`, `StoreAttr`, and `CallMethodAttr` now have working side-array
  caches. The next work here is smaller and performance-focused: keep the cache
  hit paths frameless, avoid accidental helper calls on the happy path, and add
  structural tests only where they pin down important lowering decisions.

## Language And Runtime Semantics

- [ ] Replace generic runtime failures with specific VM exceptions.

  Arithmetic overflow, type errors, unsupported descriptor paths, and other
  slow paths should raise specific exceptions rather than collapsing to generic
  runtime failures.

- [ ] Finish the iterator protocol.

  `range()` currently returns a `RangeIterator` directly. Python semantics want
  `range()` to return a range object and `iter(range_obj)` to produce the
  iterator. This is exception-gated because real iterator exhaustion must be
  represented as Python `StopIteration`.

  Note: compact no-value `StopIteration` currently preserves `not_present` so
  `yield from` can distinguish no return value from a real `None` return value.
  Python-facing `StopIteration.value` may need a descriptor or accessor later
  so ordinary user code sees `None` for the no-value case without losing that
  internal distinction.

- [ ] Expand call syntax and remaining statements.

  Useful next increments include keyword arguments, `**kwargs`, richer call
  forms, `nonlocal` declarations, `import`, `with`, `try`, and `yield`.
  Positional defaults, `*args`, and `global` declarations are already
  implemented for ordinary functions and tier-1 constructor calls. `del` is no
  longer in this bucket for the implemented simple targets: variables,
  attributes, and list/dictionary subscripts now parse, lower, and execute.
  Keyword calls for ordinary functions are the main non-exception-gated slice
  in this bucket.

## Object Model Follow-Ups

- [ ] Decide builtin instance attribute policies per native type.

  Some builtin values should reject arbitrary writes, while others may
  eventually support fixed native fields plus ordinary overflow attributes.
  Keep this expressed through Shape policy and shared object helpers.

- [ ] Finish custom attribute hooks.

  Implement `__getattribute__`, `__getattr__`, `__setattr__`, and `__delattr__`,
  and make their Shape flags disable default attribute caches unless a
  specialized hook cache exists.

- [ ] Add observable bound-method objects for escaped method values.

  Direct `CallMethodAttr` calls should stay allocation-free, but `f = obj.m`
  must eventually expose a real callable with the receiver bound according to
  descriptor protocol.

## Memory Management

- [ ] Implement runtime scanning of heap layout value regions.

  The layout metadata describes where scanned `Value` cells live, but the
  runtime scanner/reclaimer still needs to use it broadly.

- [ ] Write a staged plan for refcounting and safepoints.

  Turn [doc/refcounting-and-safepoints.md](./refcounting-and-safepoints.md)
  into an implementation sequence: deferred refcounting invariants,
  zero-count tables, safepoint polling, and multi-thread coordination.

- [ ] Verify cache and validity-cell lifetime invariants.

  Inline cache side arrays, cached plans, Shapes, and validity cells must keep
  referenced heap objects alive until their owning code object or class state no
  longer needs them.
