"""Binary file I/O and buffered reading.

Text streams and builtin open are not yet implemented. Owning streams must be
closed explicitly or with a with statement; abandonment does not close them.
"""

from _io import IOBase, RawIOBase, BufferedIOBase, FileIO, UnsupportedOperation
from _io import BufferedReader, DEFAULT_BUFFER_SIZE

__all__ = ["IOBase", "RawIOBase", "BufferedIOBase", "FileIO", "UnsupportedOperation",
           "BufferedReader", "DEFAULT_BUFFER_SIZE"]
