"""Bounded Base64 encoding and decoding for bytes and ASCII strings.

Uses stored payloads; subclass encode/translate overrides and general buffers
are outside this slice.
"""
# Wrappers and decoder state machine adapted from CPython v3.14.5,
# Lib/base64.py and Modules/binascii.c.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import binascii

__all__ = ["b64encode", "b64decode", "standard_b64encode",
           "standard_b64decode", "urlsafe_b64encode", "urlsafe_b64decode"]

_alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
_decode_table = []
for _i in range(256):
    _decode_table.append(-1)
for _i in range(64):
    _decode_table[_alphabet[_i]] = _i


def _encode_input(s):
    if not isinstance(s, bytes):
        raise TypeError("a bytes-like object is required, not '" +
                        s.__class__.__name__ + "'")
    return bytes.__getitem__(s, slice(None))


def _bytes_from_decode_data(s):
    if isinstance(s, str):
        result = []
        for i in range(str.__len__(s)):
            ch = ord(str.__getitem__(s, i))
            if ch > 127:
                raise ValueError("string argument should contain only ASCII characters")
            result.append(ch)
        return bytes(result)
    if isinstance(s, bytes):
        # Wrapper-level length assertions must still observe subclass hooks.
        return s
    raise TypeError("argument should be a bytes-like object or ASCII string, not '" +
                    s.__class__.__name__ + "'")


def _translate(s, source, target):
    # Simultaneous translation; a duplicate source byte uses its last mapping.
    # Both alphabets are normalized bytes here, so check their stored lengths.
    if len(source) != len(target):
        raise ValueError("maketrans arguments must have same length")
    table = list(range(256))
    table[source[0]] = target[0]
    table[source[1]] = target[1]
    result = []
    for i in range(len(s)):
        result.append(table[s[i]])
    return bytes(result)


def b64encode(s, altchars=None):
    """Encode bytes, optionally replacing '+' and '/' with two altchars."""
    s = _encode_input(s)
    result = []
    for i in range(0, len(s), 3):
        remaining = len(s) - i
        a = s[i]
        b = s[i + 1] if remaining > 1 else 0
        c = s[i + 2] if remaining > 2 else 0
        result.append(_alphabet[a >> 2])
        result.append(_alphabet[((a & 3) << 4) | (b >> 4)])
        result.append(_alphabet[((b & 15) << 2) | (c >> 6)] if remaining > 1 else 61)
        result.append(_alphabet[c & 63] if remaining > 2 else 61)
    encoded = bytes(result)
    if altchars is not None:
        assert len(altchars) == 2, repr(altchars)
        altchars = _encode_input(altchars)
        return _translate(encoded, b"+/", altchars)
    return encoded


def b64decode(s, altchars=None, validate=False):
    """Decode bytes or ASCII text, optionally checking the alphabet strictly."""
    s = _encode_input(_bytes_from_decode_data(s))
    if altchars is not None:
        altchars = _bytes_from_decode_data(altchars)
        assert len(altchars) == 2, repr(altchars)
        s = _translate(s, _encode_input(altchars), b"+/")
    strict = True if validate else False
    result = []
    quad_pos = 0
    leftchar = 0
    pads = 0
    for i in range(len(s)):
        ch = s[i]
        if ch == 61:
            pads += 1
            if quad_pos >= 2 and quad_pos + pads <= 4:
                continue
            if not strict:
                continue
            if quad_pos == 1:
                break
            if quad_pos == 0 and i == 0:
                raise binascii.Error("Leading padding not allowed")
            raise binascii.Error("Excess padding not allowed")
        ch = _decode_table[ch]
        if ch < 0:
            if strict:
                raise binascii.Error("Only base64 data is allowed")
            continue
        if pads and strict:
            if quad_pos + pads == 4:
                raise binascii.Error("Excess data after padding")
            raise binascii.Error("Discontinuous padding not allowed")
        pads = 0
        if quad_pos == 0:
            quad_pos = 1
            leftchar = ch
        elif quad_pos == 1:
            quad_pos = 2
            result.append((leftchar << 2) | (ch >> 4))
            leftchar = ch & 15
        elif quad_pos == 2:
            quad_pos = 3
            result.append((leftchar << 4) | (ch >> 2))
            leftchar = ch & 3
        else:
            quad_pos = 0
            result.append((leftchar << 6) | ch)
            leftchar = 0
    if quad_pos == 1:
        count = len(result) // 3 * 4 + 1
        raise binascii.Error("Invalid base64-encoded string: number of data characters (" +
                             str(count) + ") cannot be 1 more than a multiple of 4")
    if quad_pos != 0 and quad_pos + pads < 4:
        raise binascii.Error("Incorrect padding")
    return bytes(result)


def standard_b64encode(s):
    """Encode bytes using the standard alphabet, without line wrapping."""
    return b64encode(s)


def standard_b64decode(s):
    """Decode the standard alphabet, ignoring non-alphabet bytes."""
    return b64decode(s)


def urlsafe_b64encode(s):
    """Encode bytes using '-' and '_' instead of '+' and '/'."""
    return _translate(b64encode(s), b"+/", b"-_")


def urlsafe_b64decode(s):
    """Decode URL-safe or standard Base64 from bytes or ASCII text."""
    s = _encode_input(_bytes_from_decode_data(s))
    return b64decode(_translate(s, b"-_", b"+/"))
