"""Pack and unpack fixed-width integers and padding."""

import sys as _sys

__all__ = [
    "Struct", "calcsize", "error", "iter_unpack", "pack", "pack_into",
    "unpack", "unpack_from",
]


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


def _buffer_type(buffer, operation):
    if isinstance(buffer, bytes):
        return bytes
    if isinstance(buffer, bytearray):
        return bytearray
    raise TypeError(operation + " requires a bytes-like object")


def _unpack_record(plan, buffer, container, start):
    order = plan[1]
    values = []
    for field in plan[4]:
        offset = start + field[0]
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


def _unpack(plan, buffer):
    container = _buffer_type(buffer, "unpack")
    size = plan[2]
    if container.__len__(buffer) != size:
        raise error("unpack requires a buffer of " + str(size) + " bytes")
    return _unpack_record(plan, buffer, container, 0)


def _buffer_offset(offset, length, size, operation):
    if not isinstance(offset, int):
        raise TypeError(operation + " offset must be an integer")
    offset = int.__index__(offset)
    packing = operation == "pack_into"
    if offset < -_SIZE_MAX - 1 or offset > _SIZE_MAX:
        if packing:
            raise IndexError("cannot fit integer into an index-sized integer")
        raise OverflowError("Python int too large to convert to C ssize_t")

    if offset < 0:
        if offset + size > 0:
            message = "no space to pack " if packing else "not enough data to unpack "
            raise error(message + str(size) + " bytes at offset " + str(offset))
        if offset < -length:
            raise error("offset " + str(offset) + " out of range for " +
                        str(length) + "-byte buffer")
        offset += length
    if length - offset < size:
        action = "packing" if packing else "unpacking"
        raise error(operation + " requires a buffer of at least " +
                    str(size + offset) + " bytes for " + action + " " +
                    str(size) + " bytes at offset " + str(offset) +
                    " (actual buffer size is " + str(length) + ")")
    return offset


def _pack_into(plan, args):
    count = len(args)
    if count == 0:
        raise error("pack_into expected buffer argument")
    if count == 1:
        raise error("pack_into expected offset argument")
    arity = plan[3]
    if count - 2 != arity:
        raise error("pack_into expected " + str(arity) +
                    " items for packing (got " + str(count - 2) + ")")
    buffer = args[0]
    if not isinstance(buffer, bytearray):
        raise TypeError("pack_into requires a writable bytearray")
    size = plan[2]
    start = _buffer_offset(args[1], bytearray.__len__(buffer), size, "pack_into")

    # Clear the record before converting fields. Only successfully encoded
    # fields replace their zeros, and earlier writes survive a later error.
    for offset in range(start, start + size):
        buffer[offset] = 0

    order = plan[1]
    arg = 2
    for field in plan[4]:
        offset = start + field[0]
        width = field[1]
        code = field[2]
        count = field[3]
        signed = code in "bhilq"
        for _ in range(count):
            chunk = _pack_integer(args[arg], code, width, order, signed)
            for octet in chunk:
                buffer[offset] = octet
                offset += 1
            arg += 1


def _unpack_from(plan, buffer, offset):
    container = _buffer_type(buffer, "unpack_from")
    start = _buffer_offset(offset, container.__len__(buffer), plan[2],
                           "unpack_from")
    return _unpack_record(plan, buffer, container, start)


class _UnpackIterator:
    def __init__(self, plan, buffer, length):
        self._plan = plan
        self._buffer = buffer
        self._length = length
        self._offset = 0

    def __iter__(self):
        return self

    def __length_hint__(self):
        if self._buffer is None:
            return 0
        return (self._length - self._offset) // self._plan[2]

    def __next__(self):
        offset = self._offset
        if offset == self._length:
            self._buffer = None
            self._plan = None
            raise StopIteration
        record = _unpack_record(self._plan, self._buffer, bytes, offset)
        self._offset = offset + self._plan[2]
        return record


def _iter_unpack(plan, buffer):
    size = plan[2]
    if size == 0:
        raise error("cannot iteratively unpack with a struct of length 0")
    if not isinstance(buffer, bytes):
        raise TypeError("iter_unpack requires immutable bytes")
    length = bytes.__len__(buffer)
    if length % size:
        raise error("iterative unpacking requires a buffer of a multiple of " +
                    str(size) + " bytes")
    return _UnpackIterator(plan, buffer, length)


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

    def pack_into(self, /, *args):
        """Pack values into a bytearray at the required offset."""
        return _pack_into(self._plan, args)

    def unpack(self, buffer, /):
        return _unpack(self._plan, buffer)

    def unpack_from(self, /, buffer, offset=0):
        return _unpack_from(self._plan, buffer, offset)

    def iter_unpack(self, buffer, /):
        return _iter_unpack(self._plan, buffer)


def pack(format, /, *values):
    """Return the packed bytes for the given format and values."""
    return _pack(_compile(format), values)


def pack_into(format, /, *args):
    """Pack values into a bytearray at the required offset."""
    return _pack_into(_compile(format), args)


def unpack(format, buffer, /):
    """Unpack an exactly sized buffer into a tuple."""
    return _unpack(_compile(format), buffer)


def unpack_from(format, /, buffer, offset=0):
    """Unpack one record from a buffer at offset."""
    return _unpack_from(_compile(format), buffer, offset)


def iter_unpack(format, buffer, /):
    """Iterate over the fixed-size records in immutable bytes."""
    return _iter_unpack(_compile(format), buffer)


def calcsize(format, /):
    """Return the byte size of the format."""
    return _compile(format)[2]
