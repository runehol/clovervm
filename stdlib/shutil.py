"""High-level file operations."""

__all__ = ["copyfileobj"]

COPY_BUFSIZE = 256 * 1024


def copyfileobj(fsrc, fdst, length=0):
    """Copy data from the current source position to the destination."""
    if not length:
        length = COPY_BUFSIZE

    read = fsrc.read
    write = fdst.write
    while chunk := read(length):
        write(chunk)
