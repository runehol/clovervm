# Native FileIO

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Native IOBase/RawIOBase/BufferedIOBase hierarchy, unbuffered FileIO, POSIX operations, descriptor ownership, and explicit cleanup |
| Owning layers | Native object and stream semantics; POSIX backend; Python module exports; runtime reference metadata |
| Validated against | Native IO base implementation working tree (2026-10-26) |
| Supersedes | N/A |

The `io` module exports native `IOBase`, `RawIOBase`, `BufferedIOBase`, and
`FileIO` classes through `stdlib/io.py`. The raw branch is
`IOBase -> RawIOBase -> FileIO`; `BufferedIOBase` derives directly from `IOBase`.
These relationships match the C++ native prefixes. All four allow Python
subclasses;
FileIO subclasses inherit the native file constructor and descriptor layout.
BufferedReader and BufferedWriter provide managed binary buffering; text
streams and builtin `open` are separate work.
`io.UnsupportedOperation` inherits both `OSError` and `ValueError` and uses the
existing OSError native storage.

## Layout and layers

`IOBaseObject` extends `InlineSlotObject` with one inline boolean value,
`base_closed`; `RawIOBaseObject` and `BufferedIOBaseObject` add no fields.
`FileIOObject` extends
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

All four types declare static reference spans and fixed object sizes. They are
trivially destructible and have no custom resource-release callbacks. Native
layout selection recognizes the shared prefix. RawIOBase and BufferedIOBase
can coexist in either MRO order because neither adds fields; FileIO storage wins
when present. Method receiver checks also validate class ancestry, so sharing
a prefix does not make a raw-only instance a buffered stream. Incompatible
unrelated native bases remain rejected. Python base
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

`__enter__` rejects closed streams and returns the stream. `__exit__` dynamically
calls `self.close()` and returns its result. The default close returns None and
does not suppress a body exception; an overridden close returning a truthy value
can suppress it, matching CPython. There is deliberately **no cleanup
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
- Bounded `read(size)`, `readinto(bytearray)`, bytes `write(data)`, seek/tell,
  readable/writable/seekable, fileno, close, and context management. Reads with
  omitted, None or negative size raise NotImplementedError. Inherited flush is a no-op while base-open;
  inherited isatty returns False after checking closed state. Descriptor-aware
  isatty and truncate, general buffer objects, custom openers and reinitialization
  are not implemented. RawIOBase readinto/write defaults raise
  NotImplementedError. Managed RawIOBase read/readall implementations use
  subclass readinto/read overrides; FileIO has a managed readall wrapper that
  calls the retained native FileIO.read directly. Unbounded FileIO.read remains
  deferred.
- FileIO overrides `readinto(buffer)` for bytearray destinations only. It returns
  the transferred count, 0 at EOF, or None on would-block, preserving short reads,
  destination length, and the unwritten suffix. Buffer validation precedes
  closed/readability checks. The synchronous writable borrow contains only the
  native read and errno/result capture. Requests are capped at INT_MAX bytes
  on macOS and SSIZE_MAX on other POSIX platforms, matching CPython. All VM
  allocation and error translation
  happen after it ends. No buffer exports or pinning are introduced.
- Raw transfers preserve short results; EOF returns empty bytes for read and 0
  for readinto, and would-block returns None. Interrupted open/read/write/fstat
  operations retry EINTR; Python signal-handler execution is not implemented by
  this layer.
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

## Managed raw-read fallbacks

RawIOBase.read validates the receiver and index-sized size, delegates negative
sizes to self.readall(), and otherwise allocates a bytearray for self.readinto().
Index callbacks stay in managed code. Native result validation checks the
returned count against both the original request and the destination's current
length after callbacks, then copies its filled prefix into independent bytes.
None is preserved as would-block. A resized buffer whose reported count exceeds
its current length raises ValueError instead of permitting an out-of-bounds copy.

RawIOBase.readall calls self.read in 128 KiB chunks, preserving short reads until
EOF or would-block. It returns accumulated bytes, or None when the first read
would block. Chunk lengths and payload copies use stored bytes data, bypassing
bytes-subclass conversion, iteration and length overrides. OSError with errno
EINTR is retried; other errors propagate. FileIO.readall uses the same chunk size
but bypasses subclass read/readinto overrides via the original native read.
These managed methods are installed on native classes during trusted bootstrap;
no layouts, bytecode operations, interpreter dispatch paths or resource-cleanup
hooks are added.

## BufferedIOBase

BufferedIOBase introduces no buffer storage or resource ownership. It inherits
IOBase lifecycle and capability behavior. Its abstract read/read1 defaults validate a
C int-sized argument and raise UnsupportedOperation; write and detach also
raise UnsupportedOperation. Managed readinto/readinto1 fallbacks call the
current read/read1 override with the destination's stored bytearray length.
They require stored bytes results and copy only the returned prefix, preserving
the destination length and unwritten suffix. Non-bytes results raise TypeError;
results exceeding the original request or a callback-resized destination raise
ValueError. No writable span is retained across callbacks or VM allocation.
General writable-buffer inputs and computed properties remain separate work. The regressions include shared-prefix multiple inheritance,
constructor cache invalidation, callback failures, subclass hooks, destination
resizing, and forced reclamation.


## BufferedReader

`io.BufferedReader` / `_io.BufferedReader` is a managed Python subclass of
native BufferedIOBase, defined in the retained trusted bootstrap module. Its
raw reference, bytearray buffer, cursor/end indices, capacity, and reentry flag
are ordinary attributes using existing overflow storage. No reader-specific
native layout, spare inline slots, descriptors, interpreter changes, or cleanup
hooks are introduced. DEFAULT_BUFFER_SIZE is 128 KiB.

Bounded read/readinto operations consume buffered bytes and issue raw.readinto
calls until satisfied, EOF, or would-block. Large requests bypass the internal
buffer with a temporary bytearray. Short transfers are not EOF. read1 returns
buffered bytes immediately or makes one raw read; peek fills an empty buffer at
most once and does not advance the cursor. readinto1 can combine buffered bytes
with at most one additional raw read. General writable buffers remain deferred.
read/readinto/readinto1 preserve partial results and return None if no data was
available before would-block; read1/peek return empty bytes in that case.
Unbounded read drains buffered bytes and calls raw.readall when present,
otherwise loops over raw.read(). Raw transfer-count conversion/range failures
raise OSError; a count beyond callback-resized storage raises ValueError.
Native copy helpers validate stored bounds and acquire payloads only after
allocation and callbacks have finished.

tell subtracts unread bytes from raw.tell. Relative seeks compensate for
read-ahead; seek always delegates to raw and discards buffered data only after
success. Buffered operations reject reentry and release their guard on failure.
flush is a stateless raw delegation. close runs IOBase.close and then attempts
raw.close even if flushing raises; an already-closed raw stream simply marks the
reader closed. Successful initialization resets the reader's base-close flag.
detach flushes, returns raw at its current physical position, discards unread
bytes, and makes the wrapper unusable. Abandonment never closes raw.

Computed properties remain an accepted incompatibility: raw is an ordinary
writable attribute, closed reflects the reader's own base flag, and name/mode
are omitted. External raw closure does not change reader.closed; cached data
can still be consumed, but further raw transfers reject the closed stream.
Line methods/iteration, text I/O and builtin open remain deferred.


## BufferedWriter

BufferedWriter is a managed Python subclass of native BufferedIOBase, exported
through _io and io. It owns an ordinary raw reference, bytearray queue, consumed
prefix index, capacity, and operation guard using existing overflow attributes.
Bytes and bytearray inputs are supported; mutable input is snapshotted before
raw callbacks. General buffer inputs and memoryview-based zero-copy transfers
remain deferred because FileIO.write currently accepts bytes only.

Writes smaller than capacity and fitting available queue space are accepted
without a raw write. Otherwise pending data is drained before direct writes
while at least a full buffer remains; smaller remainders are queued. A blocked
exact-capacity remainder can still be accepted in full by buffering it. Invalid
buffer-size conversion during reinitialization preserves the existing attachment
and queued output. Fast writes use physical tail space; a consumed prefix is
compacted only after another blocked flush attempt. Partial raw writes advance
immediately so later retries never resend acknowledged bytes. None means would-block. A write accepts available queue
space before raising BlockingIOError with characters_written counting bytes
accepted from that call, including queued bytes. Flush preserves the unacknowledged
suffix and reports would-block with characters_written zero. Zero from a
nonempty raw write is accepted and retried without progress, matching CPython;
a raw implementation returning zero forever can therefore loop indefinitely.
Invalid raw counts/conversions raise OSError and retain known pending output.

Flush drains queued output; it does not delegate to raw.flush. Tell adds pending
bytes to the raw position. Seek and detach flush before changing position or
attachment, preserving the queue when flushing fails. Close invokes the dynamic
flush override without holding that operation's guard, then attempts raw close
in finally. Raw callbacks during transfer/flush/seek/close are guarded against
reentry. Raw-close errors take precedence over flush errors. As with the reader,
raw is writable, closed uses the local base flag, name/mode are omitted, and
abandonment does not flush or close raw. Truncate, text streams and builtin open
remain deferred.
