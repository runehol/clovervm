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
  5. use statically observed `__init__` member initialization to seed the
     instance root Shape and default inline slot count

- [ ] Execute descriptor protocol calls through interpreter-controlled dispatch.

  Lookup now classifies descriptors and surfaces descriptor get/set/delete work
  as descriptor plans. The next semantic step is to actually call descriptor
  `__get__`, `__set__`, and `__delete__` without hiding Python bytecode
  execution inside lookup helpers.

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

- [ ] Finish the native-thunk transition.

  Fixed-arity native callables can now be ordinary `Function` objects whose
  code object runs a tiny `CallNative0`/`CallNative1`/`CallNative2` thunk.
  Remaining work: move arity checks to the `Function` call boundary, design the
  packed variable-arity convention, migrate `range` and other variable-arity
  callables, and then retire the residual `BuiltinFunction` dispatch path.

- [ ] Add another callable on top of the native function path.

  `str.__str__` and `str.__add__` proved out fixed-arity native thunks. A small
  builtin namespace callable such as `print` would validate builtin lookup,
  arity checks, and the next native calling convention.

- [ ] Finish the iterator protocol.

  `range()` currently returns a `RangeIterator` directly. Python semantics want
  `range()` to return a range object and `iter(range_obj)` to produce the
  iterator.

- [ ] Expand call syntax and remaining statements.

  Useful next increments include keyword arguments, `**kwargs`, richer call
  forms, `global`/`nonlocal` declarations, `import`, `with`, `try`, and
  `yield`. Positional defaults and `*args` are already implemented for ordinary
  functions and tier-1 constructor calls. `del` is no longer in this bucket for
  the implemented simple targets: variables, attributes, and list/dictionary
  subscripts now parse, lower, and execute.

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
