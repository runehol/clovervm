# Native FileIO

The first `io` slice exports one native `_io.FileIO` through `stdlib/io.py`.
It is an exact, non-subclassable raw binary stream; the `IOBase`/`RawIOBase`
hierarchy, buffered streams, text streams and builtin `open` are separate work.
`io.UnsupportedOperation` inherits both `OSError` and `ValueError` and uses the
existing OSError native storage.

## Layout and layers

`FileIOObject` follows the fixed-inline-slot pattern: four contiguous Python
slots (`name`, `mode`, `closed`, `closefd`) followed by `int fd`, access flags,
and cached seekability. `name` is writable; the other three attributes use the
existing read-only slot mechanism. Their class/dictionary introspection follows
CloverVM's slot model, rather than CPython's getset descriptors.

`fd == -1` is authoritative. `detach_fd()` changes that sentinel and the
Python-visible `closed` value together. The type declares a static reference
span extending `InlineSlotObject` by four values and a fixed native object size.
It is trivially destructible and has no custom resource-release callback.

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

Explicit `close()` detaches before attempting OS close. Repeated close succeeds;
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
  size raise NotImplementedError. `readinto`, truncate, flush, isatty, general
  buffer objects, custom openers and reinitialization are not implemented.
- Raw transfers preserve short results; EOF returns empty bytes and would-block
  returns None. Interrupted open/read/write/fstat operations retry EINTR; Python
  signal-handler execution is not implemented by this layer.
- FileIO leaves the host's SIGPIPE disposition and signal mask unchanged.
  When the host ignores SIGPIPE, EPIPE becomes BrokenPipeError. Under the default
  disposition a broken-pipe write can terminate the process. Embedding/startup
  signal policy is separate work; there is no per-write signal protection.
- Descriptor validation rejects EBADF and directories, tolerating other fstat
  failures as CPython does for unusual filesystems.

## Verification

Python regressions cover modes, transfers, seeking, filenames, closed state,
attribute protections, binding/validation, and context unwinding, also under
forced reclamation at every safepoint. Native tests cover exact layout metadata,
owning/borrowing, construction rollback, failed close and descriptor reuse,
non-inheritable opens, nonblocking and short pipe transfers, host signal policy,
broken-pipe errors under SIG_IGN, and descriptor survival after abandonment through reclamation and VM teardown.
