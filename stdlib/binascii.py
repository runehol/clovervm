"""Bounded CRC checksums, hex conversions, and the error shared by base64.

CRC inputs are stored bytes (including subclasses) or bytearray payloads,
with builtin integer seeds.
Hex decoders also accept ASCII strings. Hex grouping accepts builtin integers.
General buffers, custom __index__, and buffer-export resize guards are omitted.
Other conversion functions and Incomplete are not implemented yet.
"""

from _binascii import crc32 as _crc32, crc_hqx as _crc_hqx


# Keep the conversion operations independent of later builtin rebinding.
from builtins import (bytes as _bytes, bytearray as _bytearray, str as _str,
                      int as _int, isinstance as _isinstance, len as _len,
                      ord as _ord, range as _range, slice as _slice,
                      TypeError as _TypeError, ValueError as _ValueError,
                      OverflowError as _OverflowError)

_missing = object()
_hex_digits = b"0123456789abcdef"


class Error(ValueError):
    __module__ = "binascii"


def crc32(data, crc=0, /):
    """Compute an unsigned CRC-32, optionally continuing a previous checksum."""
    return _crc32(data, crc)


def crc_hqx(data, crc, /):
    """Compute an unsigned CRC-CCITT using the supplied initial checksum."""
    return _crc_hqx(data, crc)


def _check_hex_data(data):
    if not (_isinstance(data, _bytes) or _isinstance(data, _bytearray)):
        raise _TypeError("a bytes-like object is required")


def _hex_input(data):
    if _isinstance(data, _bytearray):
        return _bytes(data)
    return _bytes.__getitem__(data, _slice(None))


def _hexlify(data, sep, bytes_per_sep):
    _check_hex_data(data)
    # Explicit builtin conversion ignores int-subclass hooks; custom __index__
    # is outside this slice, as it is for the CRC seed APIs.
    if not _isinstance(bytes_per_sep, _int):
        raise _TypeError("bytes_per_sep must be an integer")
    group = _int.__index__(bytes_per_sep)
    if group < -2147483648 or group > 2147483647:
        raise _OverflowError("Python int too large to convert to C int")
    separator = 0
    if sep is _missing:
        group = 0
    else:
        # CPython tests length before type, even when grouping is disabled.
        if _len(sep) != 1:
            raise _ValueError("sep must be length 1.")
        if _isinstance(sep, _str):
            # CPython checks the storage width of the entire string, even
            # when an overridden __len__ reports one for a longer payload.
            stored_size = _str.__len__(sep)
            for i in _range(stored_size):
                if _ord(_str.__getitem__(sep, i)) > 255:
                    raise _ValueError("sep must be ASCII.")
            separator = _ord(_str.__getitem__(sep, 0)) if stored_size else 0
        elif _isinstance(sep, _bytes):
            # An empty stored payload with reported length one uses CPython's
            # zero terminator as the separator.
            separator = _bytes.__getitem__(sep, 0) if _bytes.__len__(sep) else 0
        else:
            raise _TypeError("sep must be str or bytes.")
    # Separator length can call Python and mutate data. Snapshot afterwards.
    data = _hex_input(data)
    size = _len(data)
    result = []
    width = -group if group < 0 else group
    for i in _range(size):
        if i and width:
            boundary = i if group < 0 else size - i
            if boundary % width == 0:
                result.append(separator)
        ch = data[i]
        result.append(_hex_digits[ch >> 4])
        result.append(_hex_digits[ch & 15])
    return _bytes(result)


def hexlify(data, sep=_missing, bytes_per_sep=1):
    """Encode bytes as lowercase hex, optionally grouping with a separator."""
    return _hexlify(data, sep, bytes_per_sep)


def b2a_hex(data, sep=_missing, bytes_per_sep=1):
    """Encode bytes as lowercase hex; equivalent to hexlify."""
    return _hexlify(data, sep, bytes_per_sep)


def _unhexlify(hexstr):
    if _isinstance(hexstr, _str):
        values = []
        for i in _range(_str.__len__(hexstr)):
            ch = _ord(_str.__getitem__(hexstr, i))
            if ch > 127:
                raise _ValueError("string argument should contain only ASCII characters")
            values.append(ch)
        data = _bytes(values)
    else:
        _check_hex_data(hexstr)
        data = _hex_input(hexstr)
    size = _len(data)
    if size % 2:
        raise Error("Odd-length string")
    result = []
    for i in _range(0, size, 2):
        pair = 0
        for j in _range(2):
            ch = data[i + j]
            if 48 <= ch <= 57:
                digit = ch - 48
            elif 65 <= ch <= 70:
                digit = ch - 55
            elif 97 <= ch <= 102:
                digit = ch - 87
            else:
                raise Error("Non-hexadecimal digit found")
            pair = pair * 16 + digit
        result.append(pair)
    return _bytes(result)


def unhexlify(hexstr, /):
    """Decode an even number of ASCII hex digits to bytes."""
    return _unhexlify(hexstr)


def a2b_hex(hexstr, /):
    """Decode hex bytes or ASCII text; equivalent to unhexlify."""
    return _unhexlify(hexstr)
