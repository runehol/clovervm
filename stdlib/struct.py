"""Pack and unpack fixed-width integers and padding."""

import sys as _sys

__all__ = ["Struct", "calcsize", "error", "pack", "unpack"]


class error(Exception):
    """Invalid format, packing argument, or buffer size."""


_WIDTHS = {
    "b": 1, "B": 1,
    "h": 2, "H": 2,
    "i": 4, "I": 4,
    "l": 4, "L": 4,
    "q": 8, "Q": 8,
    "x": 1,
}
_ORDERS = {"<": "little", ">": "big", "!": "big", "=": _sys.byteorder}
_WHITESPACE = " \t\n\r\v\f"
# The parser's size limit is independent of CloverVM's smaller bytes storage
# limit, which sys.maxsize currently reports.
_SIZE_MAX = (1 << 63) - 1
_COUNT_QUOTIENT = _SIZE_MAX // 10
_COUNT_REMAINDER = _SIZE_MAX % 10
_BYTES_MAX = _sys.maxsize


def _format_text(format):
    if isinstance(format, str):
        text = str.__getitem__(format, slice(None))
        if not text.isascii():
            start = 0
            while ord(text[start]) < 128:
                start += 1
            end = start + 1
            while end < len(text) and ord(text[end]) >= 128:
                end += 1
            raise UnicodeEncodeError("ascii", format, start, end,
                                     "ordinal not in range(128)")
        return text
    if isinstance(format, bytes):
        # Latin-1 preserves each input byte so the shared parser can reject
        # high bytes as bad format characters.
        return bytes.decode(format, "latin-1")
    raise TypeError("Struct() argument 1 must be a str or bytes object")


def _compile(format):
    text = _format_text(format)
    if "\x00" in text:
        raise error("embedded null character")

    prefix = text[:1]
    native = prefix not in _ORDERS
    order = _ORDERS["="] if native else _ORDERS[prefix]
    index = 1 if prefix == "@" or not native else 0
    length = len(text)
    size = 0
    arity = 0
    fields = []
    while index < length:
        code = text[index]
        index += 1
        if code in _WHITESPACE:
            continue
        count = 1
        if "0" <= code <= "9":
            count = 0
            while "0" <= code <= "9":
                digit = ord(code) - ord("0")
                if count > _COUNT_QUOTIENT or (
                    count == _COUNT_QUOTIENT and digit > _COUNT_REMAINDER
                ):
                    raise error("total struct size too long")
                count = count * 10 + digit
                if index == length:
                    raise error("repeat count given without format specifier")
                code = text[index]
                index += 1

        if code not in _WIDTHS:
            raise error("bad char in struct format")
        if native and code not in "bBx":
            raise error("native struct format requires an explicit standard-size prefix")
        width = _WIDTHS[code]
        if count > (_SIZE_MAX - size) // width:
            raise error("total struct size too long")
        if code != "x" and count != 0:
            fields.append((size, width, code, count))
            # Each value uses at least one byte, so arity is bounded by size.
            arity += count
        size += count * width

    return text, order, size, arity, tuple(fields)


def _pack_integer(value, code, width, order, signed):
    if not isinstance(value, int):
        raise error("required argument is not an integer")
    try:
        return int.to_bytes(value, width, order, signed=signed)
    except OverflowError:
        pass
    limit = 1 << (8 * width - (1 if signed else 0))
    minimum = -limit if signed else 0
    raise error("'" + code + "' format requires " + str(minimum) +
                " <= number <= " + str(limit - 1))


def _pack(plan, values):
    order = plan[1]
    size = plan[2]
    arity = plan[3]
    fields = plan[4]
    if len(values) != arity:
        raise error("pack expected " + str(arity) + " items for packing (got " +
                    str(len(values)) + ")")
    if size > _BYTES_MAX:
        raise OverflowError("byte string is too large")

    chunks = []
    padding = []
    previous_end = 0
    arg = 0
    for field in fields:
        offset = field[0]
        width = field[1]
        code = field[2]
        count = field[3]
        if offset > previous_end:
            # Record the gap without allocating its payload until every
            # value has passed conversion.
            padding.append((len(chunks), offset - previous_end))
            chunks.append(None)
        signed = code in "bhilq"
        for _ in range(count):
            chunks.append(_pack_integer(values[arg], code, width, order, signed))
            arg += 1
        previous_end = offset + width * count
    for gap in padding:
        chunks[gap[0]] = bytes(gap[1])
    if size > previous_end:
        chunks.append(bytes(size - previous_end))
    return b"".join(chunks)


def _unpack(plan, buffer):
    order = plan[1]
    size = plan[2]
    fields = plan[4]
    if isinstance(buffer, bytes):
        container = bytes
    elif isinstance(buffer, bytearray):
        container = bytearray
    else:
        raise TypeError("unpack requires a bytes-like object")
    if container.__len__(buffer) != size:
        raise error("unpack requires a buffer of " + str(size) + " bytes")

    values = []
    for field in fields:
        offset = field[0]
        width = field[1]
        code = field[2]
        count = field[3]
        signed = code in "bhilq"
        for _ in range(count):
            end = offset + width
            chunk = container.__getitem__(buffer, slice(offset, end))
            values.append(int.from_bytes(chunk, order, signed=signed))
            offset = end
    return tuple(values)


class Struct:
    """A reusable struct format."""

    def __init__(self, /, format):
        self._plan = _compile(format)

    @property
    def format(self):
        return self._plan[0]

    @property
    def size(self):
        return self._plan[2]

    def pack(self, /, *values):
        return _pack(self._plan, values)

    def unpack(self, buffer, /):
        return _unpack(self._plan, buffer)


def pack(format, /, *values):
    """Return the packed bytes for the given format and values."""
    return _pack(_compile(format), values)


def unpack(format, buffer, /):
    """Unpack an exactly sized buffer into a tuple."""
    return _unpack(_compile(format), buffer)


def calcsize(format, /):
    """Return the byte size of the format."""
    return _compile(format)[2]
