"""High-level file operations."""

import io
import os
import stat


__all__ = ["copyfileobj", "copyfile", "copy", "Error", "SameFileError",
           "SpecialFileError"]

COPY_BUFSIZE = 256 * 1024


class Error(OSError):
    pass


class SameFileError(Error):
    pass


class SpecialFileError(OSError):
    pass


def copyfileobj(fsrc, fdst, length=0):
    """Copy data from the current source position to the destination."""
    if not length:
        length = COPY_BUFSIZE

    read = fsrc.read
    write = fdst.write
    while chunk := read(length):
        write(chunk)


def _check_path(path):
    if not isinstance(path, str):
        raise TypeError("shutil requires string paths")
    # The POSIX os helpers pass UTF-8 paths to C string APIs.
    if str.find(path, "\x00") != -1:
        raise ValueError("embedded null character in path")


def _samefile(src, dst):
    try:
        return os.path.samefile(src, dst)
    except OSError:
        return False


def _check_fifo(path):
    try:
        mode = os.stat(path)[stat.ST_MODE]
    except OSError:
        return
    if stat.S_ISFIFO(mode):
        raise SpecialFileError("".join(("`", str(path), "` is a named pipe")))


def copyfile(src, dst, *, follow_symlinks=True):
    """Copy file contents between POSIX string paths and return dst.

    With follow_symlinks=False, a symbolic link source raises NotImplementedError.
    """
    _check_path(src)
    _check_path(dst)
    if _samefile(src, dst):
        raise SameFileError("".join((repr(src), " and ", repr(dst), " are the same file")))
    _check_fifo(src)
    _check_fifo(dst)
    if not follow_symlinks and os.path.islink(src):
        raise NotImplementedError("shutil cannot recreate symbolic links")

    with io.FileIO(src, "rb") as fsrc:
        try:
            # Own the raw destination even if BufferedWriter construction fails.
            with io.FileIO(dst, "wb") as raw:
                with io.BufferedWriter(raw) as fdst:
                    copyfileobj(fsrc, fdst)
        except IsADirectoryError:
            if not os.path.exists(dst):
                raise FileNotFoundError("".join(("Directory does not exist: ", str(dst))))
            raise
    return dst


def copy(src, dst, *, follow_symlinks=True):
    """Copy file contents and mode bits, resolving a directory destination."""
    # Validate before basename/join and before isdir can inspect the C prefix.
    _check_path(src)
    _check_path(dst)
    if os.path.isdir(dst):
        dst = os.path.join(dst, os.path.basename(src))
    copyfile(src, dst, follow_symlinks=follow_symlinks)
    os.chmod(dst, stat.S_IMODE(os.stat(src)[stat.ST_MODE]))
    return dst
