# Portions adapted from CPython's Lib/urllib/parse.py, Python 3.14.10.
# Copyright (c) Python Software Foundation.
# Derived portions are licensed under the PSF License Agreement.
"""Bounded URL percent conversion, referenced to CPython 3.14.10.

Inputs use stored str/bytes/subclass payloads or builtin bytearrays, without
subclass transformation hooks. Text encoding supports the VM's UTF-8/Latin-1
strict codecs. URL parsing, text unquoting, query helpers and networking are
not implemented. General buffers and arbitrary input conversion protocols are
outside this slice.
"""

from builtins import (bytes as _bytes, bytearray as _bytearray, str as _str,
                      isinstance as _isinstance, len as _len, ord as _ord,
                      range as _range, slice as _slice, chr as _chr,
                      TypeError as _TypeError)

__all__ = ['quote_from_bytes', 'unquote_to_bytes', 'quote', 'quote_plus']
_hex = '0123456789ABCDEF'
_unreserved = b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~'


def _binary(data):
    if data.__class__ is _bytes:
        return data
    if _isinstance(data, _bytearray):
        return _bytes(data)
    return _bytes.__getitem__(data, _slice(None))


def _safe_bytes(safe):
    values = []
    if _isinstance(safe, _str):
        # This API needs ASCII filtering, not an ASCII codec or error handler.
        for i in _range(_str.__len__(safe)):
            ch = _ord(_str.__getitem__(safe, i))
            if ch < 128:
                values.append(ch)
    else:
        for ch in safe:
            if ch < 128:
                values.append(ch)
    return _bytes(values)


def quote_from_bytes(bs, safe='/'):
    """Percent-encode bytes or bytearray, returning an ASCII string."""
    if not (_isinstance(bs, _bytes) or _isinstance(bs, _bytearray)):
        raise _TypeError('quote_from_bytes() expected bytes')
    if _isinstance(bs, _bytearray):
        size = _bytearray.__len__(bs)
    else:
        size = _bytes.__len__(bs)
    if not size:
        return ''
    safe = _safe_bytes(safe)
    # Safe iterators may mutate or reenter. Consume the payload afterwards.
    data = _binary(bs)
    allowed = []
    for i in _range(256):
        allowed.append(False)
    for ch in _unreserved:
        allowed[ch] = True
    for ch in safe:
        allowed[ch] = True
    result = []
    for ch in data:
        if allowed[ch]:
            result.append(_chr(ch))
        else:
            result.append('%' + _hex[ch >> 4] + _hex[ch & 15])
    return ''.join(result)


def quote(string, safe='/', encoding=None, errors=None):
    """Percent-encode text (UTF-8/Latin-1 strict) or binary data."""
    if _isinstance(string, _str):
        if not _str.__len__(string):
            return string
        if encoding is None:
            encoding = 'utf-8'
        if errors is None:
            errors = 'strict'
        string = _str.encode(string, encoding, errors)
    else:
        if encoding is not None:
            raise _TypeError("quote() doesn't support 'encoding' for bytes")
        if errors is not None:
            raise _TypeError("quote() doesn't support 'errors' for bytes")
    return quote_from_bytes(string, safe)


def quote_plus(string, safe='', encoding=None, errors=None):
    """Percent-encode, using '+' for spaces and no default safe slash."""
    if _isinstance(string, _str):
        plain = _str.__getitem__(string, _slice(None))
        if ' ' not in plain:
            return quote(string, safe, encoding, errors)
    elif _isinstance(string, _bytes):
        if b' ' not in _binary(string):
            return quote(string, safe, encoding, errors)
    if _isinstance(safe, _str):
        space = ' '
    else:
        space = b' '
    return quote(string, safe + space, encoding, errors).replace(' ', '+')


def _digit(ch):
    if 48 <= ch <= 57:
        return ch - 48
    if 65 <= ch <= 70:
        return ch - 55
    if 97 <= ch <= 102:
        return ch - 87
    return -1


def unquote_to_bytes(string):
    """Decode valid percent escapes, leaving other bytes unchanged."""
    if _isinstance(string, _str):
        data = _str.encode(string, 'utf-8', 'strict')
    elif _isinstance(string, _bytes) or _isinstance(string, _bytearray):
        data = _binary(string)
    else:
        # Match ordinary invalid-input AttributeErrors without accepting a
        # general split/conversion protocol.
        string.split
        raise _TypeError('unquote_to_bytes() expected str, bytes or bytearray')
    if b'%' not in data:
        return data
    if _isinstance(string, _bytearray):
        # CPython 3.14.10 hashes bytearray slices in its hex lookup, raising
        # even for malformed/incomplete escapes. Preserve that public error.
        raise _TypeError("cannot use 'bytearray' as a dict key (unhashable type: 'bytearray')")
    result = []
    i = 0
    size = _len(data)
    while i < size:
        if data[i] == 37 and i + 2 < size:
            high = _digit(data[i + 1])
            low = _digit(data[i + 2])
            if high >= 0 and low >= 0:
                result.append(high * 16 + low)
                i += 3
                continue
        result.append(data[i])
        i += 1
    return _bytes(result)
