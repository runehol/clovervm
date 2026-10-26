# Native FileIO

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Native IOBase/RawIOBase hierarchy, unbuffered FileIO, POSIX operations, descriptor ownership, and explicit cleanup |
| Owning layers | Native object and stream semantics; POSIX backend; Python module exports; runtime reference metadata |
| Validated against | Native IO base implementation working tree (2026-10-26) |
| Supersedes | N/A |

The `io` module exports native `IOBase`, `RawIOBase`, and `FileIO` classes
through `stdlib/io.py`. The Python hierarchy is `IOBase -> RawIOBase -> FileIO`,
matching the C++ native prefix hierarchy. All three allow Python subclasses;
FileIO subclasses inherit the native file constructor and descriptor layout.
Buffered streams, text streams and builtin `open` are separate work.
`io.UnsupportedOperation` inherits both `OSError` and `ValueError` and uses the
existing OSError native storage.

## Layout and layers

`IOBaseObject` extends `InlineSlotObject` with one inline boolean value,
`base_closed`; `RawIOBaseObject` adds no fields. `FileIOObject` extends
`RawIOBaseObject` with four contiguous Python slots (`name`, `mode`, `closed`,
`closefd`), followed by `int fd`, access flags, and cached seekability.
`name` is writable; the other three FileIO attributes use the existing read-only
slot mechanism. Base instances expose `closed` at slot 0; FileIO reserves that
slot privately and exposes descriptor closed state at slot 3. Introspection
follows CloverVM's slot model rather than CPython's getset descriptors.

`fd == -1` is authoritative for FileIO descriptor state. `detach_fd()` changes
that sentinel and the Python-visible `closed` value together. Base close state
is independent: directly calling `IOBase.close(file)` marks the base closed
without closing the descriptor. Inherited `flush` checks the private base flag;
`_checkClosed` and context entry check the public `closed` attribute.

All three types declare static reference spans and fixed object sizes. They are
trivially destructible and have no custom resource-release callbacks. Native
layout selection recognizes this prefix chain and retains the most derived
storage provider, rejecting incompatible unrelated native bases. Python base
subclasses use the inherited native allocator; a reclaimable constructor thunk
allocates the native layout before calling a custom Python `__init__`. The
existing constructor MRO validity cell guards that decision. General custom
`__new__` plus `__init__` support remains separate work, including a custom
Python `__init__` paired with FileIO's inherited native constructor.

`builtin_types/file_io.cpp` owns Python argument validation, byte allocation,
mode/state checks, ownership transitions, and OSError translation.
`platform/file_io_posix.cpp` owns descriptor operations and captures errno in
plain result structures. It does not allocate VM objects or set Python pending
exceptions. CMake selects the backend. A future Windows CRT backend can retain
the same descriptor layout; there is no platform subclass or per-object dispatch.

## Ownership and cleanup

Path construction opens a non-inheritable descriptor. A local C++ guard owns it
until validation and initialization finish. Failure closes that new descriptor
without replacing the original pending exception. Existing descriptors transfer
ownership only after successful construction; failure leaves them with the
caller. `closefd=False` is supported only for supplied descriptors.

`IOBase.close()` dynamically calls `self.flush()` and sets the private base
closed flag even if flush raises; repeated base close skips flushing. FileIO
performs this base phase, then detaches before attempting OS close, even if
flushing failed. If both fail, the close error wins (exception-context chaining
remains unsupported). Detachment happens after the flush callback so reentrant
close cannot close a descriptor twice. Repeated close succeeds;
a failed OS close raises OSError and leaves the stream closed. Close is not
retried after EINTR because the numeric descriptor may already have been reused.
Borrowing streams detach without closing the caller's descriptor.

`__enter__` rejects closed streams and returns the stream. `__exit__` closes the
stream and does not suppress a body exception. There is deliberately **no cleanup
on abandonment, heap reclamation, or VM teardown**. Unclosed descriptors remain
open until process exit. No finalizer, GC hook, or resource registry is installed.

## Supported operations and boundaries

- String paths encoded as UTF-8, with embedded NUL rejection, or stored integer
  descriptors fitting C `int`. Custom path/index protocols and bytes paths are
  unsupported. Oversized descriptors raise OverflowError; CPython may instead
  attempt path conversion and raise TypeError. Bool descriptors are accepted
  without CPython's RuntimeWarning.
- Modes `r`, `w`, `x`, `a`, optional `+`, and binary markers, including repeated
  `b` markers accepted by CPython FileIO. Update `w+` reports mode `rb+`.
- Bounded `read(size)`, bytes `write(data)`, seek/tell, readable/writable/seekable,
  fileno, close, and context management. Reads with omitted, None or negative
  size raise NotImplementedError. Inherited flush is a no-op while base-open;
  inherited isatty returns False after checking closed state. Descriptor-aware
  isatty and truncate, general buffer objects, custom openers and reinitialization
  are not implemented. RawIOBase read/readinto/write defaults raise
  NotImplementedError; generic read/readall fallbacks remain deferred.
- Raw transfers preserve short results; EOF returns empty bytes and would-block
  returns None. Interrupted open/read/write/fstat operations retry EINTR; Python
  signal-handler execution is not implemented by this layer.
- FileIO leaves the host's SIGPIPE disposition and signal mask unchanged.
  When the host ignores SIGPIPE, EPIPE becomes BrokenPipeError. Under the default
  disposition a broken-pipe write can terminate the process. The public startup
  config can install SIGPIPE ignore, and the CLI explicitly selects that policy;
  the embedding default leaves signals unchanged. There is no per-write signal
  protection.
- Descriptor validation rejects EBADF and directories, tolerating other fstat
  failures as CPython does for unusual filesystems.

## Verification

Python regressions cover modes, transfers, seeking, filenames, closed state,
attribute protections, binding/validation, and context unwinding, also under
forced reclamation at every safepoint. Native tests cover exact layout metadata,
owning/borrowing, construction rollback, failed close and descriptor reuse,
non-inheritable opens, nonblocking and short pipe transfers, host signal policy,
broken-pipe errors under SIG_IGN, and descriptor survival after abandonment through reclamation and VM teardown.

Base regressions additionally cover Python subclass initialization and attributes,
compatible and incompatible multiple inheritance, constructor invalidation and
reclaimable helpers, independent base/descriptor closed state, dynamic capability
checks and context exit, failing/reentrant flush, and close-error precedence.
