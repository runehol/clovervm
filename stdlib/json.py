"""String-based JSON encoding and decoding for CloverVM.

This subset follows CPython 3.14.5 for builtin values and ordinary function
callbacks. File APIs, bytes input, encoder/decoder classes, cls, indentation,
skipkeys, parse_constant, object_pairs_hook, and strict=False are not provided.
Nesting (including default conversions) is limited to 512; exceeding it raises
RuntimeError under this module's explicit cap until VM recursion guards are
available. Integer conversion
uses the VM's bigint support without CPython's configurable decimal digit cap;
finite float formatting uses the VM's builtin representation.
The private _json module supplies exact-dict detection and live entry cursors;
parsing and encoding policy remain in this module.

Object names must be quoted strings; duplicate names retain their last value.
Only space, tab, CR, and LF are whitespace. Standard JSON escapes are accepted;
surrogate pairs combine and lone surrogates are preserved. NaN and signed
Infinity are accepted on input and controlled by allow_nan on output. Encoder
keys may be str, int, float, bool, or None, sorted before string conversion when
requested.
Callback exceptions propagate. Error messages and positions follow CPython's
accelerated decoder, without exception notes or pickling compatibility.
Unsorted exact-dict callbacks may change the key set, but traversal during
growth/compaction follows CloverVM's storage layout and can differ from CPython.
Exact output/callback parity is not guaranteed for such key-set mutations;
callbacks that only replace existing values do not have this limitation.
Exact lists returned by dict subclass items() remain shared and are sorted in
place; other items() iterables are materialized before encoding.
"""

# Escape and diagnostic behavior informed by CPython v3.14.5 Lib/json/decoder.py
# and Lib/json/encoder.py. Portions adapted from that PSF-licensed material.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

from _json import is_exact_dict as _is_exact_dict, dict_next as _dict_next

__all__ = ["loads", "dumps", "JSONDecodeError"]

_MAX_DEPTH = 512
_DIGITS = "0123456789"
_HEX = "0123456789abcdef"
_UNESCAPE = {
    '"': '"', "\\": "\\", "/": "/", "b": "\b", "f": "\f",
    "n": "\n", "r": "\r", "t": "\t",
}
_ESCAPE = {
    '"': '\\"', "\\": "\\\\", "\b": "\\b", "\f": "\\f",
    "\n": "\\n", "\r": "\\r", "\t": "\\t",
}


class JSONDecodeError(ValueError):
    """Invalid JSON, with character offset and one-based line and column."""

    def __init__(self, msg, doc, pos):
        line = doc.count("\n", 0, pos) + 1
        col = pos - doc.rfind("\n", 0, pos)
        ValueError.__init__(
            self, msg + ": line " + str(line) + " column " + str(col)
            + " (char " + str(pos) + ")"
        )
        self.msg = msg
        self.doc = doc
        self.pos = pos
        self.lineno = line
        self.colno = col


class _Parser:
    def __init__(self, doc, object_hook, parse_float, parse_int):
        self.original_doc = doc
        self.doc = str.__str__(doc)
        self.end = len(self.doc)
        self.pos = 0
        self.object_hook = object_hook
        self.parse_float = parse_float or float
        self.parse_int = parse_int or int

    def _error(self, msg, pos):
        raise JSONDecodeError(msg, self.original_doc, pos)

    def _peek(self):
        if self.pos < self.end:
            return self.doc[self.pos]
        return ""

    def _space(self):
        while self.pos < self.end and self.doc[self.pos] in " \t\r\n":
            self.pos += 1

    def _hex4(self, pos):
        if pos + 4 > self.end:
            self._error("Invalid \\uXXXX escape", pos - 1)
        result = 0
        idx = pos
        while idx < pos + 4:
            ch = self.doc[idx]
            if "0" <= ch and ch <= "9":
                digit = ord(ch) - ord("0")
            elif "a" <= ch and ch <= "f":
                digit = ord(ch) - ord("a") + 10
            elif "A" <= ch and ch <= "F":
                digit = ord(ch) - ord("A") + 10
            else:
                self._error("Invalid \\uXXXX escape", pos - 1)
            result = result * 16 + digit
            idx += 1
        return result

    def _string(self):
        start = self.pos
        self.pos += 1
        chunks = []
        run = self.pos
        while self.pos < self.end:
            ch = self.doc[self.pos]
            if ch == '"':
                chunks.append(self.doc[run:self.pos])
                self.pos += 1
                return "".join(chunks)
            if ord(ch) < 32:
                self._error("Invalid control character at", self.pos)
            if ch != "\\":
                self.pos += 1
                continue
            chunks.append(self.doc[run:self.pos])
            slash = self.pos
            self.pos += 1
            if self.pos == self.end:
                self._error("Unterminated string starting at", start)
            escape = self.doc[self.pos]
            if escape == "u":
                code = self._hex4(self.pos + 1)
                self.pos += 5
                if 55296 <= code and code <= 56319:
                    if self.doc[self.pos:self.pos + 2] == "\\u":
                        low = self._hex4(self.pos + 2)
                        if 56320 <= low and low <= 57343:
                            code = 65536 + (code - 55296) * 1024 + low - 56320
                            self.pos += 6
                chunks.append(chr(code))
            elif escape in _UNESCAPE:
                chunks.append(_UNESCAPE[escape])
                self.pos += 1
            else:
                self._error("Invalid \\escape", slash)
            run = self.pos
        self._error("Unterminated string starting at", start)

    def _number(self):
        start = self.pos
        idx = start
        if self.doc[idx] == "-":
            idx += 1
        if idx == self.end:
            self._error("Expecting value", start)
        if self.doc[idx] == "0":
            idx += 1
        elif "1" <= self.doc[idx] and self.doc[idx] <= "9":
            idx += 1
            while idx < self.end and self.doc[idx] in _DIGITS:
                idx += 1
        else:
            self._error("Expecting value", start)
        is_float = False
        if idx + 1 < self.end and self.doc[idx] == ".":
            if self.doc[idx + 1] in _DIGITS:
                is_float = True
                idx += 2
                while idx < self.end and self.doc[idx] in _DIGITS:
                    idx += 1
        if idx < self.end and self.doc[idx] in "eE":
            exponent = idx + 1
            if exponent < self.end and self.doc[exponent] in "+-":
                exponent += 1
            if exponent < self.end and self.doc[exponent] in _DIGITS:
                is_float = True
                idx = exponent + 1
                while idx < self.end and self.doc[idx] in _DIGITS:
                    idx += 1
        token = self.doc[start:idx]
        self.pos = idx
        if is_float:
            return self.parse_float(token)
        return self.parse_int(token)

    def _array(self, depth):
        self.pos += 1
        self._space()
        result = []
        if self._peek() == "]":
            self.pos += 1
            return result
        while True:
            result.append(self._value(depth + 1))
            self._space()
            ch = self._peek()
            if ch == "]":
                self.pos += 1
                return result
            if ch != ",":
                self._error("Expecting ',' delimiter", self.pos)
            comma = self.pos
            self.pos += 1
            self._space()
            if self._peek() == "]":
                self._error("Illegal trailing comma before end of array", comma)

    def _object(self, depth):
        self.pos += 1
        self._space()
        result = {}
        if self._peek() != "}":
            while True:
                if self._peek() != '"':
                    self._error(
                        "Expecting property name enclosed in double quotes", self.pos
                    )
                key = self._string()
                self._space()
                if self._peek() != ":":
                    self._error("Expecting ':' delimiter", self.pos)
                self.pos += 1
                self._space()
                result[key] = self._value(depth + 1)
                self._space()
                ch = self._peek()
                if ch == "}":
                    break
                if ch != ",":
                    self._error("Expecting ',' delimiter", self.pos)
                comma = self.pos
                self.pos += 1
                self._space()
                if self._peek() == "}":
                    self._error("Illegal trailing comma before end of object", comma)
        self.pos += 1
        if self.object_hook is not None:
            return self.object_hook(result)
        return result

    def _value(self, depth):
        ch = self._peek()
        if depth >= _MAX_DEPTH and (ch == "[" or ch == "{"):
            raise RuntimeError("Maximum JSON nesting depth exceeded")
        if ch == '"':
            return self._string()
        if ch == "[":
            return self._array(depth)
        if ch == "{":
            return self._object(depth)
        for token in ("null", "true", "false", "NaN", "Infinity", "-Infinity"):
            if self.doc[self.pos:self.pos + len(token)] == token:
                self.pos += len(token)
                if token == "null":
                    return None
                if token == "true":
                    return True
                if token == "false":
                    return False
                if token == "NaN":
                    return float("nan")
                if token == "Infinity":
                    return float("inf")
                return float("-inf")
        if ch != "" and ch in "-0123456789":
            return self._number()
        self._error("Expecting value", self.pos)


def loads(s, *, object_hook=None, parse_float=None, parse_int=None):
    """Decode a JSON str; optional callbacks receive dicts or number tokens."""
    if not isinstance(s, str):
        raise TypeError("the JSON object must be str")
    if s.startswith(chr(65279)):
        raise JSONDecodeError("Unexpected UTF-8 BOM (decode using utf-8-sig)", s, 0)
    parser = _Parser(s, object_hook, parse_float, parse_int)
    parser._space()
    result = parser._value(0)
    parser._space()
    if parser.pos != len(s):
        raise JSONDecodeError("Extra data", s, parser.pos)
    return result


def _hex_escape(code):
    return (
        "\\u" + _HEX[(code >> 12) & 15] + _HEX[(code >> 8) & 15]
        + _HEX[(code >> 4) & 15] + _HEX[code & 15]
    )


def _sort_items_in_place(items):
    # CPython retains an exact list returned by items() and sorts that list.
    # Comparisons see it empty. Detect writes left there during sorting.
    original = list(items)
    items.clear()
    try:
        ordered = _sort_items(original)
    except:
        items.clear()
        items.extend(original)
        raise
    modified = len(items) != 0
    items.clear()
    items.extend(ordered)
    if modified:
        raise ValueError("list modified during sort")


def _sort_items(items):
    # Sort snapshotted pairs before key coercion using builtin ordering,
    # including values when a dict subclass returns duplicate keys.
    if len(items) < 2:
        return items
    mid = len(items) // 2
    left = _sort_items(items[:mid])
    right = _sort_items(items[mid:])
    result = []
    i = 0
    j = 0
    while i < len(left) and j < len(right):
        if right[j] < left[i]:
            result.append(right[j])
            j += 1
        else:
            result.append(left[i])
            i += 1
    result.extend(left[i:])
    result.extend(right[j:])
    return result


class _Encoder:
    def __init__(self, ensure_ascii, check_circular, allow_nan, separators,
                 default, sort_keys):
        self.ensure_ascii = ensure_ascii
        self.check_circular = check_circular
        self.allow_nan = allow_nan
        self.default = default
        self.sort_keys = sort_keys
        self.item_separator = ", "
        self.key_separator = ": "
        if separators is not None:
            # Tuple unpacking requests at most three items. In particular,
            # do not exhaust arbitrary or unbounded separator iterators.
            iterator = iter(separators)
            parts = []
            for idx in range(3):
                try:
                    part = next(iterator)
                except StopIteration:
                    break
                parts.append(part)
            if len(parts) != 2:
                raise ValueError("separators must contain exactly two values")
            self.item_separator = parts[0]
            self.key_separator = parts[1]
        self.chunks = []

    def _string(self, value):
        value = str.__str__(value)
        self.chunks.append('"')
        run = 0
        idx = 0
        while idx < len(value):
            ch = value[idx]
            code = ord(ch)
            escape = None
            if ch in _ESCAPE:
                escape = _ESCAPE[ch]
            elif code < 32 or (self.ensure_ascii and code > 126):
                if code <= 65535:
                    escape = _hex_escape(code)
                else:
                    code -= 65536
                    escape = (
                        _hex_escape(55296 + (code >> 10))
                        + _hex_escape(56320 + (code & 1023))
                    )
            if escape is not None:
                self.chunks.append(value[run:idx])
                self.chunks.append(escape)
                run = idx + 1
            idx += 1
        self.chunks.append(value[run:idx])
        self.chunks.append('"')

    def _float(self, value):
        value = float.__float__(value)
        if value != value:
            text = "NaN"
        elif value == float("inf"):
            text = "Infinity"
        elif value == float("-inf"):
            text = "-Infinity"
        else:
            return float.__repr__(value)
        if not self.allow_nan:
            raise ValueError("Out of range float values are not JSON compliant")
        return text

    def _key(self, key):
        if isinstance(key, str):
            return key
        if key is True:
            return "true"
        if key is False:
            return "false"
        if key is None:
            return "null"
        if isinstance(key, int):
            return int.__repr__(key)
        if isinstance(key, float):
            return self._float(key)
        raise TypeError("keys must be str, int, float, bool or None")

    def _pair(self, key, value, ancestors, depth, first):
        if not first:
            self.chunks.append(self.item_separator)
        self._string(self._key(key))
        self.chunks.append(self.key_separator)
        self._value(value, ancestors, depth + 1)

    def _value(self, obj, ancestors, depth):
        if depth > _MAX_DEPTH:
            raise RuntimeError("Maximum JSON nesting depth exceeded")
        if obj is None:
            self.chunks.append("null")
        elif obj is True:
            self.chunks.append("true")
        elif obj is False:
            self.chunks.append("false")
        elif isinstance(obj, str):
            self._string(obj)
        elif isinstance(obj, int):
            self.chunks.append(int.__repr__(obj))
        elif isinstance(obj, float):
            self.chunks.append(self._float(obj))
        else:
            if depth >= _MAX_DEPTH:
                raise RuntimeError("Maximum JSON nesting depth exceeded")
            sequence = None
            if isinstance(obj, list) or isinstance(obj, tuple):
                sequence = obj
                if obj.__class__ is not list and obj.__class__ is not tuple:
                    # CPython materializes iterable subclasses before checking
                    # circularity or invoking default on their contents.
                    sequence = list(obj)
                if len(sequence) == 0:
                    self.chunks.append("[]")
                    return
            elif isinstance(obj, dict) and dict.__len__(obj) == 0:
                # CPython bypasses markers and items() for an empty dictionary,
                # including an ancestor emptied by a default callback.
                self.chunks.append("{}")
                return
            if self.check_circular:
                for ancestor in ancestors:
                    if obj is ancestor:
                        raise ValueError("Circular reference detected")
                ancestors.append(obj)
            if sequence is not None:
                self.chunks.append("[")
                first = True
                for item in sequence:
                    if not first:
                        self.chunks.append(self.item_separator)
                    first = False
                    self._value(item, ancestors, depth + 1)
                self.chunks.append("]")
            elif isinstance(obj, dict):
                self.chunks.append("{")
                first = True
                if not self.sort_keys and _is_exact_dict(obj):
                    # Keep the raw entry cursor across callbacks. Normal
                    # Python dict iterators reject the permitted mutations.
                    cursor = 0
                    while True:
                        entry = _dict_next(obj, cursor)
                        if entry is None:
                            break
                        cursor = entry[0]
                        self._pair(entry[1], entry[2], ancestors, depth, first)
                        first = False
                else:
                    items = obj.items()
                    if items.__class__ is not list:
                        items = list(items)
                    if self.sort_keys:
                        _sort_items_in_place(items)
                    for item in items:
                        if not isinstance(item, tuple) or len(item) != 2:
                            raise ValueError("items must return 2-tuples")
                        self._pair(item[0], item[1], ancestors, depth, first)
                        first = False
                self.chunks.append("}")
            else:
                if self.default is None:
                    raise TypeError("Object is not JSON serializable")
                self._value(self.default(obj), ancestors, depth + 1)
            if self.check_circular:
                ancestors.pop()


def dumps(obj, *, ensure_ascii=True, check_circular=True, allow_nan=True,
          separators=None, default=None, sort_keys=False):
    """Encode supported builtin values as a JSON str."""
    # Match the public wrapper's short-circuited cached-encoder selection.
    # Stateful truth values can be observed here before encoder setup.
    cached = False
    if ensure_ascii:
        if check_circular:
            if allow_nan:
                if (separators is None and default is None and not sort_keys):
                    cached = True
    if cached:
        encoder = _Encoder(True, True, True, None, None, False)
    else:
        encoder = _Encoder(ensure_ascii, check_circular, allow_nan, separators,
                           default, sort_keys)
    if isinstance(obj, str):
        # CPython's root-string shortcut never constructs the native encoder.
        encoder.ensure_ascii = True if encoder.ensure_ascii else False
    else:
        # Freeze the choices before callbacks or traversal, in setup order.
        encoder.check_circular = True if encoder.check_circular else False
        encoder.ensure_ascii = True if encoder.ensure_ascii else False
        if (not isinstance(encoder.key_separator, str)
                or not isinstance(encoder.item_separator, str)):
            raise TypeError("separators must be strings")
        encoder.sort_keys = True if encoder.sort_keys else False
        encoder.allow_nan = True if encoder.allow_nan else False
    encoder._value(obj, [], 0)
    return "".join(encoder.chunks)
