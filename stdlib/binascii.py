"""Bounded CRC checksums and the error shared by base64.

CRC inputs are stored bytes (including subclasses) or bytearray payloads,
with builtin integer seeds. General buffers and custom __index__ are omitted.
Other conversion functions and Incomplete are not implemented yet.
"""

from _binascii import crc32 as _crc32, crc_hqx as _crc_hqx


class Error(ValueError):
    __module__ = "binascii"


def crc32(data, crc=0, /):
    """Compute an unsigned CRC-32, optionally continuing a previous checksum."""
    return _crc32(data, crc)


def crc_hqx(data, crc, /):
    """Compute an unsigned CRC-CCITT using the supplied initial checksum."""
    return _crc_hqx(data, crc)
