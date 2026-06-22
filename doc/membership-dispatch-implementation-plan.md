# Membership Dispatch Implementation Plan

This plan stages `in` and `not in` as an explicit membership protocol opcode
followed by explicit truthiness conversion. The goal is to fix the currently
emitted-but-unhandled membership bytecode without hiding Python-visible
membership behavior inside generic fallback machinery.

## Semantic Shape

For:

```python
needle in container
needle not in container
```

the bytecode contract is:

```text
container = accumulator
needle = fp[needle_reg]
```

Membership first attempts:

```python
type(container).__contains__(container, needle)
```

If `__contains__` is missing, membership calls a VM-owned fallback helper. The
fallback is selected only by a missing `__contains__` lookup. A returned
`NotImplemented` value is not a fallback signal and does not use an operator
continuation opcode.

`TestIn` leaves the selected call result in the accumulator. The compiler then
emits `ToBool` for `in` or `ToBoolNot` for `not in`. Those truthiness opcodes
use the current VM truthiness behavior. Full `__bool__` / `__len__` truthiness
dispatch is deferred to the broader implicit protocol work.

## Bytecode Shape

Keep `TestIn` as the membership protocol opcode and have it carry an operator
inline-cache index:

```text
TestIn needle_reg, operator_ic
```

Add explicit truthiness conversion opcodes:

```text
ToBool
ToBoolNot
```

Compile:

```python
needle in container
```

as:

```text
TestIn needle_reg, operator_ic
ToBool
```

Compile:

```python
needle not in container
```

as:

```text
TestIn needle_reg, operator_ic
ToBoolNot
```

`TestNotIn` is no longer part of the target bytecode shape. Stage 4 removes
normal emission of it and deletes the opcode support once all in-repo lowering
uses `TestIn + ToBoolNot`.

## Operator Table

Add a membership table:

```cpp
OperatorDispatchTableId::Contains
```

Add a membership-specific table fallback verb:

```cpp
OperatorStepAction::CallMembershipFallback
```

Install the table as:

```text
0: CallBinary("__contains__", IfMethodFound)
1: CallMembershipFallback(Always)
```

This is intentionally not a `RaiseUnsupported` fallback. Missing
`__contains__` is a normal membership protocol branch.

## Builtin Container Support

Implement `__contains__` methods for the common builtin container types:

- `list.__contains__(self, needle)`;
- `tuple.__contains__(self, needle)`;
- `dict.__contains__(self, key)`;
- `str.__contains__(self, needle)`.

These methods provide the Python-visible protocol targets used by the
membership opcode. They should also install trusted handlers so hot membership
sites can skip the untrusted Python-call path when the guarded lookup proves
the builtin method is still selected.

Trusted handlers should preserve the current container semantics available in
the VM:

- `list` and `tuple` search their elements using the equality behavior already
  supported by the container methods;
- `dict` tests key presence and keeps the current string-key dictionary
  limitations unless general dictionary keys have landed;
- `str` tests substring membership for string needles and raises the ordinary
  unsupported-type error for non-string needles.

The trusted handlers are optimizations of visible `__contains__` dispatch, not
separate builtin ladders. The cache must still guard the container shape and
the `__contains__` lookup validity cell before calling them.

## Cache Contract

Reuse `OperatorInlineCache`, but match it as a unary cache on the container:

```text
cache guard arity = unary
cache operand     = container
runtime call args = (container, needle)
```

The cache entry should be populated as:

```text
operand_shape_keys[0] = ShapeKey(container)
operand_shape_keys[1] = ShapeKey(Value::not_present())

operand_lookup_validity_cells[0] =
    positive or negative __contains__ lookup validity cell
operand_lookup_validity_cells[1] = nullptr

function = resolved __contains__ function or VM-owned fallback function
code_object = function->code_object
n_args = 2
adaptation = function_call_adaptation_for_positional_call(function, 2)
has_self = true for __contains__, false for fallback helper
resume_index = unused
reflected_untrusted_call = false
trusted_handler = null for the first slice
```

The opcode must not use `matches_binary(container, needle)`. The needle shape is
ordinary runtime call state and must not participate in cache matching.

`TestIn` does not truth-test or invert the call result. It stages the membership
call so that normal interpreter return lands on the following `ToBool` or
`ToBoolNot` opcode.

## Miss Path

On a cache miss:

1. Walk `OperatorDispatchTableId::Contains` with:

   ```text
   operand0 = container
   operand1 = needle
   cacheability = CacheableDirectOnly
   ```

2. If row 0 finds a function-shaped `__contains__`, return an untrusted
   function-call descriptor that calls with `(container, needle)` and caches on
   the container lookup validity cell only.

3. If row 0 misses, row 1 `CallMembershipFallback` returns an untrusted
   function-call descriptor for the VM-owned fallback helper. It must cache on
   the negative `__contains__` lookup validity cell so adding `__contains__` to
   the container class invalidates the fallback cache.

4. Found-but-unsupported `__contains__` cases follow the existing operator IC
   limits. This slice does not add descriptor execution or arbitrary
   callable-object support.

## Fallback Helper

Define the first helper in `src/bootstrap/builtins.py`, then capture it during
VM bootstrap and delete the name from the builtins scope before user code runs.
The opcode must call the VM-owned function directly, not look it up through
builtins at runtime. User code must not be able to shadow the helper.

Initial helper:

```python
def __clover_membership_fallback(container, needle):
    for item in container:
        if item == needle:
            return True
    return False
```

Sequence-index fallback is staged for later. When added, catch only failure to
obtain an iterator, not arbitrary errors raised during iteration or equality.

## Truthiness Conversion

Add `ToBool` and `ToBoolNot` opcode handlers. They should:

- consume the accumulator;
- write `True` or `False` back to the accumulator;
- use the same current truthiness behavior as `not` and conditional jumps;
- support float truthiness through the existing float slow-path behavior;
- raise the current unsupported-truthiness error for unsupported pointer
  results;
- differ only in whether the boolean result is inverted.

These opcodes are not membership-specific. They are reusable continuation
opcodes for any future operation that must convert an arbitrary result to a
VM-level boolean.

## Interpreter Dispatch

Add a membership-specific `TestIn` opcode helper instead of reusing
`dispatch_cached_direct_binary_operator`, because that helper checks both
operand shapes. The membership helper should:

1. Decode `needle_reg` and `operator_ic`.
2. Load `container` from the accumulator and `needle` from `fp[needle_reg]`.
3. If `cache.matches_unary(container)`, stage `(container, needle)` and enter
   the cached function with the next bytecode as the return target.
4. On miss, walk the membership table and populate the cache from the returned
   descriptor.
5. Stage the selected call with `(container, needle)` and enter the function
   with the next bytecode as the return target.

No `CheckOperatorNotImplemented` or other continuation opcode is involved.
Truthiness conversion is handled by the following `ToBool` or `ToBoolNot`
opcode.

## Tests

Add interpreter tests for:

- `1 in [1, 2]` returns `True`;
- `3 in [1, 2]` returns `False`;
- `3 not in [1, 2]` returns `True`;
- a class with `__contains__` is called with the searched item;
- a cached `__contains__` hit does not depend on the searched item shape;
- adding or replacing `__contains__` invalidates a cached fallback;
- missing `__contains__` uses the fallback helper for iterable objects;
- `__contains__` returning a non-bool value uses current truthiness behavior;
- `__contains__` returning `NotImplemented` does not continue to fallback.

Add codegen tests that `in` and `not in` emit:

```text
TestIn needle_reg, operator_ic[n]
ToBool

TestIn needle_reg, operator_ic[n]
ToBoolNot
```

Add a direct regression for the current failure:

```bash
build-debug/src/clovervm -c "1 in [1, 2]"
```

## Staging Plan

### Stage 1: Bytecode and Table Skeleton

- [x] Extend `TestIn` and `TestNotIn` to include an operator IC operand.
- [x] Add `OperatorDispatchTableId::Contains`.
- [x] Add `OperatorStepAction::CallMembershipFallback`.
- [x] Install the membership table:

  ```text
  0: CallBinary("__contains__", IfMethodFound)
  1: CallMembershipFallback(Always)
  ```

- [x] Add codegen tests that prove `in` and `not in` emit the new opcode shape.

This stage may still route execution to a controlled unsupported path if the
runtime call path is not wired yet, but it must not leave the opcodes as
unknown dispatch-table entries.

### Stage 2: VM-Owned Fallback Helper

- [x] Add `__clover_membership_fallback(container, needle)` to
  `src/bootstrap/builtins.py`.
- [x] During VM bootstrap, resolve the helper to a stable VM-owned function handle.
- [x] Delete the helper name from the builtins module namespace before user code can
  observe or shadow it.
- [x] Expose an internal accessor for the operator walker to retrieve the fallback
  function.
- [x] Add tests that the helper is not visible through normal builtins lookup
  after bootstrap.

The Stage 1 `TestNotIn` skeleton was temporary scaffolding. The revised target
shape removes it in Stage 4 rather than completing it.

### Stage 3: Truthiness Conversion Bytecodes

- [ ] Add `ToBool` and `ToBoolNot` bytecodes.
- [ ] Add code object printer support for `ToBool` and `ToBoolNot`.
- [ ] Add `ToBool` and `ToBoolNot` interpreter handlers using current VM
  truthiness behavior.
- [ ] Add focused interpreter tests for immediate values, floats, and
  unsupported pointer truthiness.

This stage should not wire membership dispatch. It gives later membership
lowering a concrete post-call truthiness target and keeps the truthiness
semantics reviewable on their own.

### Stage 4: Membership Opcode Dispatch

- [ ] Change `not in` codegen to emit `TestIn ...; ToBoolNot`.
- [ ] Change `in` codegen to emit `TestIn ...; ToBool`.
- [ ] Remove `TestNotIn` from normal codegen and delete the obsolete opcode
  support.
- [ ] Add the `TestIn` interpreter handler.
- [ ] Implement the membership-specific cache hit path using
  `OperatorInlineCache::matches_unary(container)`.
- [ ] Stage cached calls with `(container, needle)` and return to the next
  bytecode.
- [ ] Implement the miss path by walking `OperatorDispatchTableId::Contains`.
- [ ] Populate the operator cache for both positive `__contains__` lookup and
  negative-lookup fallback helper calls.
- [ ] Add codegen tests for `TestIn + ToBool` and `TestIn + ToBoolNot`.
- [ ] Add a regression test for:

  ```bash
  build-debug/src/clovervm -c "1 in [1, 2]"
  ```

for containers that already have a visible `__contains__` method or can use the
fallback helper.

### Stage 5: Builtin `__contains__` Methods

- [ ] Add visible `list.__contains__`.
- [ ] Add visible `tuple.__contains__`.
- [ ] Add visible `dict.__contains__`.
- [ ] Add visible `str.__contains__`.
- [ ] Keep behavior aligned with each type's current VM-supported semantics:
  element search for `list` and `tuple`, string-key presence for current `dict`,
  and substring membership for `str`.
- [ ] Add Python/interpreter tests for direct method calls such as:

  ```python
  [1, 2].__contains__(1)
  {"x": 1}.__contains__("x")
  "abc".__contains__("b")
  ```

This stage makes the protocol target visible independently of the membership
opcode.

### Stage 6: Trusted Builtin Handlers

- [ ] Add trusted handler resolution for builtin `list.__contains__`.
- [ ] Add trusted handler resolution for builtin `tuple.__contains__`.
- [ ] Add trusted handler resolution for builtin `dict.__contains__`.
- [ ] Add trusted handler resolution for builtin `str.__contains__`.
- [ ] Ensure the membership opcode only calls trusted handlers after the ordinary
  `__contains__` lookup guards prove the builtin method is selected.
- [ ] Add cache-hit tests for trusted membership on common containers.
- [ ] Add invalidation tests showing class or method mutation deoptimizes away from
  stale trusted membership decisions.

This stage is an optimization of the protocol path, not a semantic shortcut.

### Stage 7: Deferred Protocol Polish

- [ ] Add sequence-index fallback to the hidden helper after the iterator fallback
  path is stable.
- [ ] Replace current truthiness behavior with the eventual shared truthiness
  protocol dispatch when that work lands.
- [ ] Revisit docs once membership fallback, truthiness, and general dictionary keys
  become more complete.

## Documentation Follow-Up

After implementation, update:

- `doc/fast-operator-dispatch.md` to describe membership as a receiver-owned
  binary call using unary `OperatorIC` cacheability.
- `doc/development-priorities.md` to replace the basic `in` / `not in` gap with
  the remaining deferred membership work.
- `doc/python-deviations.md` if sequence-index fallback or full truthiness
  protocol dispatch remains user-visible.
