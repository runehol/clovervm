"""Native unbuffered binary file I/O.

Buffered and text streams and builtin open are not yet
implemented. Owning streams must be closed explicitly or with a with statement;
abandoning a stream does not close its descriptor.
"""

from _io import IOBase, RawIOBase, BufferedIOBase, FileIO, UnsupportedOperation

__all__ = ["IOBase", "RawIOBase", "BufferedIOBase", "FileIO", "UnsupportedOperation"]
