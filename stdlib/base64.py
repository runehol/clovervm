"""Bounded Base16, Base32, Base64, Base85, and Ascii85 conversion for bytes and bytearray.

Uses stored payloads; subclass length, indexing, and transformation overrides
are outside this slice, except explicit altchars/map01 length assertions.
Decoders also accept ASCII strings. General buffers and native buffer-export
resize guards are unsupported.
"""
# Wrappers and decoder state machine adapted from CPython v3.14.5,
# Lib/base64.py and Modules/binascii.c.
# Bytearray support and callback ordering checked against CPython v3.14.10.
# Base85/Ascii85 semantics referenced to CPython v3.14.10, Lib/base64.py.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import binascii

__all__ = ["b64encode", "b64decode", "standard_b64encode",
           "standard_b64decode", "urlsafe_b64encode", "urlsafe_b64decode",
           "b16encode", "b16decode", "b32encode", "b32decode",
           "b32hexencode", "b32hexdecode", "b85encode", "b85decode",
           "a85encode", "a85decode"]

_alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
_decode_table = []
for _i in range(256):
    _decode_table.append(-1)
for _i in range(64):
    _decode_table[_alphabet[_i]] = _i


def _encode_input(s):
    if isinstance(s, bytearray):
        return bytes(s)
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
    if isinstance(s, bytes) or isinstance(s, bytearray):
        # Wrapper-level length assertions must still observe subclass hooks.
        return s
    raise TypeError("argument should be a bytes-like object or ASCII string, not '" +
                    s.__class__.__name__ + "'")


def _decode_input(s):
    s = _bytes_from_decode_data(s)
    # Normalize immutable data without hooks, but keep mutable data live until
    # the wrapper's option callbacks have run. Option length assertions use
    # _bytes_from_decode_data directly so they still see bytes-subclass hooks.
    # Downstream code can reuse the resulting exact bytes without another copy.
    if isinstance(s, bytes):
        return bytes.__getitem__(s, slice(None))
    return s


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
    s = _decode_input(s)
    if altchars is not None:
        altchars = _bytes_from_decode_data(altchars)
        assert len(altchars) == 2, repr(altchars)
        if isinstance(s, bytearray):
            s = bytes(s)
        s = _translate(s, _encode_input(altchars), b"+/")
    strict = True if validate else False
    # With altchars, translation already captured the input before validate.
    # Otherwise validate's truth test can still mutate the bytearray payload.
    if isinstance(s, bytearray):
        s = bytes(s)
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


_b16alphabet = b"0123456789ABCDEF"
_b32alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
_b32hexalphabet = b"0123456789ABCDEFGHIJKLMNOPQRSTUV"
_b32_decode_table = []
_b32hex_decode_table = []
for _i in range(256):
    _b32_decode_table.append(-1)
    _b32hex_decode_table.append(-1)
for _i in range(32):
    _b32_decode_table[_b32alphabet[_i]] = _i
    _b32hex_decode_table[_b32hexalphabet[_i]] = _i


def _fold_ascii(s):
    result = []
    for i in range(len(s)):
        ch = s[i]
        result.append(ch - 32 if 97 <= ch <= 122 else ch)
    return bytes(result)


def b16encode(s):
    """Encode stored bytes using the uppercase Base16 alphabet."""
    s = _encode_input(s)
    result = []
    for i in range(len(s)):
        ch = s[i]
        result.append(_b16alphabet[ch >> 4])
        result.append(_b16alphabet[ch & 15])
    return bytes(result)


def b16decode(s, casefold=False):
    """Decode Base16 bytes or ASCII text, optionally allowing lowercase."""
    s = _decode_input(s)
    # A false flag can also mutate the input; test it before taking a snapshot.
    fold = True if casefold else False
    if isinstance(s, bytearray):
        s = bytes(s)
    if fold:
        s = _fold_ascii(s)
    digits = []
    for i in range(len(s)):
        ch = s[i]
        if 48 <= ch <= 57:
            digits.append(ch - 48)
        elif 65 <= ch <= 70:
            digits.append(ch - 55)
        else:
            raise binascii.Error("Non-base16 digit found")
    if len(s) % 2:
        raise binascii.Error("Odd-length string")
    result = []
    for i in range(0, len(digits), 2):
        result.append((digits[i] << 4) | digits[i + 1])
    return bytes(result)


def _b32encode(alphabet, s):
    s = _encode_input(s)
    result = []
    acc = 0
    bits = 0
    for i in range(len(s)):
        ch = s[i]
        acc = (acc << 8) | ch
        bits += 8
        while bits >= 5:
            bits -= 5
            result.append(alphabet[(acc >> bits) & 31])
        # Keep only the partial symbol, independent of input length.
        acc &= (1 << bits) - 1
    if bits:
        result.append(alphabet[acc << (5 - bits)])
    while len(result) % 8:
        result.append(61)
    return bytes(result)


def _b32decode(table, s, casefold=False, map01=None):
    s = _decode_input(s)
    if len(s) % 8:
        raise binascii.Error("Incorrect padding")
    if map01 is not None:
        map01 = _bytes_from_decode_data(map01)
        assert len(map01) == 1, repr(map01)
        # The assertion sees subclass hooks; translation checks stored length.
        if isinstance(s, bytearray):
            s = bytes(s)
        s = _translate(s, b"01", b"O" + _encode_input(map01))
    # Translation has already frozen the input when map01 was supplied.
    fold = True if casefold else False
    if isinstance(s, bytearray):
        s = bytes(s)
    if fold:
        s = _fold_ascii(s)
    end = len(s)
    while end and s[end - 1] == 61:
        end -= 1
    pads = len(s) - end
    result = []
    acc = 0
    bits = 0
    for i in range(end):
        digit = table[s[i]]
        if digit < 0:
            raise binascii.Error("Non-base32 digit found")
        acc = (acc << 5) | digit
        bits += 5
        if bits >= 8:
            bits -= 8
            result.append((acc >> bits) & 255)
        acc &= (1 << bits) - 1
    # Validate after alphabet errors, and accept unused nonzero bits like CPython.
    # Option callbacks may have resized mutable input after the first check.
    if len(s) % 8 or pads not in (0, 1, 3, 4, 6):
        raise binascii.Error("Incorrect padding")
    return bytes(result)


def b32encode(s):
    """Encode stored bytes using the standard padded Base32 alphabet."""
    return _b32encode(_b32alphabet, s)


def b32decode(s, casefold=False, map01=None):
    """Decode Base32, optionally folding case and mapping digits 0 and 1."""
    return _b32decode(_b32_decode_table, s, casefold, map01)


def b32hexencode(s):
    """Encode stored bytes using the extended hexadecimal Base32 alphabet."""
    return _b32encode(_b32hexalphabet, s)


def b32hexdecode(s, casefold=False):
    """Decode extended hexadecimal Base32, optionally folding lowercase."""
    return _b32decode(_b32hex_decode_table, s, casefold)


_b85alphabet = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~"
_b85_decode_table = []
for _i in range(256):
    _b85_decode_table.append(-1)
for _i in range(85):
    _b85_decode_table[_b85alphabet[_i]] = _i


def _85encode(b, alphabet, pad, ascii85=False, foldspaces=False):
    # Snapshot before any option callbacks; all words come from this payload.
    b = _encode_input(b)
    result = []
    length = len(b)
    for i in range(0, length, 4):
        word = 0
        for j in range(4):
            word = (word << 8) | (b[i + j] if i + j < length else 0)
        if ascii85:
            if not word:
                result.append(122)
                continue
            # Capture one truth test per nonzero word, like the other wrappers.
            fold = True if foldspaces else False
            if fold and word == 538976288:
                result.append(121)
                continue
        for divisor in (52200625, 614125, 7225, 85, 1):
            result.append(alphabet[word // divisor % 85])
    padding = (-length) % 4
    # Only partial groups test pad, after consuming all words and foldspaces.
    if padding and not pad:
        if ascii85 and result[-1] == 122:
            # A partial zero group must expand z before dropping tail digits.
            result.pop()
            result.extend((33, 33, 33, 33, 33))
        result = result[:-padding]
    return bytes(result)


def b85encode(b, pad=False):
    """Encode stored bytes using Base85, optionally keeping zero padding."""
    return _85encode(b, _b85alphabet, pad)


def b85decode(b):
    """Decode Base85 bytes or ASCII text, rejecting bad digits and overflow."""
    b = _encode_input(_bytes_from_decode_data(b))
    result = []
    length = len(b)
    for i in range(0, length, 5):
        acc = 0
        for j in range(5):
            # Missing tail digits are virtual '~' characters (digit 84).
            digit = _b85_decode_table[b[i + j]] if i + j < length else 84
            if digit < 0:
                raise ValueError("bad base85 character at position " + str(i + j))
            acc = acc * 85 + digit
        # Check after all five digits, so invalid characters take precedence
        # over overflow within the same group, but not over an earlier group.
        if acc > 4294967295:
            raise ValueError("base85 overflow in hunk starting at byte " + str(i))
        for shift in (24, 16, 8, 0):
            result.append((acc >> shift) & 255)
    padding = (-length) % 5
    if padding:
        result = result[:-padding]
    return bytes(result)


_a85alphabet = b"!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstu"


def a85encode(b, *, foldspaces=False, wrapcol=0, pad=False, adobe=False):
    """Encode stored bytes as Ascii85, with optional folding, wrapping and framing."""
    result = _85encode(b, _a85alphabet, pad, True, foldspaces)
    if adobe:
        result = b'<~' + result
    if wrapcol:
        wrapcol = max(2 if adobe else 1, wrapcol)
        # Validate before iterating, even for empty output. Bound native range
        # and slice arguments by the output size, allowing huge builtin widths.
        from operator import index as _index
        step = _index(wrapcol)
        length = len(result)
        if step < 0:
            step = -1
        elif step > length:
            step = length if length else 1
        chunks = []
        for i in range(0, length, step):
            end = i + wrapcol
            if end is not None:
                end = _index(end)
                if end > length:
                    end = length
                elif end < -length:
                    end = 0
            chunks.append(result[i:end])
        if adobe:
            if len(chunks[-1]) + 2 > wrapcol:
                chunks.append(b'')
        result = b'\n'.join(chunks)
    if adobe:
        result += b'~>'
    return result


def a85decode(b, *, foldspaces=False, adobe=False, ignorechars=b' \t\n\r\v'):
    """Decode Ascii85 bytes or ASCII text, allowing selected ignored characters."""
    b = _decode_input(b)
    if adobe:
        # Adobe's truth callback sees live mutable data; framing then freezes it.
        b = _encode_input(b)
        if not b.endswith(b'~>'):
            raise ValueError("Ascii85 encoded byte sequences must end with b'~>'")
        if b.startswith(b'<~'):
            b = b[2:-2]
        else:
            b = b[:-2]
    else:
        b = _encode_input(b)
    result = []
    acc = 0
    count = 0
    # Four literal sentinel digits preserve short-tail overflow validation.
    # Scanning this immutable copy isolates subsequent callbacks from input edits.
    b = b + b'uuuu'
    for i in range(len(b)):
        ch = b[i]
        if 33 <= ch <= 117:
            acc = acc * 85 + ch - 33
            count += 1
            if count == 5:
                if acc > 4294967295:
                    raise ValueError('Ascii85 overflow')
                for shift in (24, 16, 8, 0):
                    result.append((acc >> shift) & 255)
                acc = 0
                count = 0
        elif ch == 122:
            if count:
                raise ValueError('z inside Ascii85 5-tuple')
            result.extend((0, 0, 0, 0))
        else:
            fold = True if foldspaces else False
            if fold and ch == 121:
                if count:
                    raise ValueError('y inside Ascii85 5-tuple')
                result.extend((32, 32, 32, 32))
            elif ch in ignorechars:
                continue
            else:
                raise ValueError('Non-Ascii85 digit found: ' + chr(ch))
    padding = 4 - count
    if padding:
        result = result[:-padding]
    return bytes(result)
