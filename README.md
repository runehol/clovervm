# clovervm - Clover Virtual Machine
Experiments with JIT compilation. Very much work-in-progress.

## Current status

`clovervm` can already parse, compile, and interpret a small Python-like
subset. The project is still exploratory, so unsupported constructs are common
and some runtime behavior is intentionally narrow.

## Currently supported subset

- Module-style execution of expressions and simple statement sequences.
- Integer, boolean, and `None` values.
- Names, assignment, and augmented assignment such as `+=`.
- Function definitions, parameter passing, calls, recursion, and `return`.
- `if` / `elif` / `else` statements.
- `while` loops, including `break`, `continue`, and loop `else`.
- Arithmetic and bitwise operators:
  `+`, `-`, `*`, `/`, `//`, `%`, `**`, `<<`, `>>`, `|`, `&`, `^`, unary `+`,
  unary `-`, `~`.
- Boolean operators and comparisons:
  `not`, `and`, `or`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `is`, `is not`, `in`,
  `not in`.
- Tuple syntax in the parser for comma-separated expression lists.
- Tokenization for names, indentation-sensitive blocks, double-quoted string
  literals, and integer literal spellings including decimal, hexadecimal,
  octal, binary, and underscores.

## Known limitations

- Much of Python is not implemented yet, including `import`, `class`, `for`,
  `try`, `with`, `global`, `nonlocal`, `del`, and `yield`.
- Assignment is currently much narrower than Python. In practice, codegen only
  supports simple variable targets cleanly.
- String support is minimal. The tokenizer recognizes basic double-quoted
  literals, but string semantics are not close to full Python yet.
- Arithmetic currently focuses on tagged small integers. Overflow and several
  non-small-integer paths still raise generic VM exceptions instead of matching
  CPython behavior.
- Error messages and runtime semantics are improving, but compatibility is not
  yet a project guarantee.

## Planned design points

- Python 3 source language.
- V8-inspired register/accumulator byte code.
- Value representation with integers, bools and None represented without indirection and memory allocation.
- JIT compilation to native code for selected architectures.
- Type feedback for specialising the native code.
- Hidden classes to handle Python objects with C struct-like performance.
- Python C API compatibility for supporting the extension ecosystem.
- Memory management using deferred refcounting - only references on the heap are refcounted, not references on the stack. This means only stores into objects trigger refcounting.
- No GIL, fast multithreading.
