"""Pretty-print lists, tuples and dictionaries."""

# The two-pass layout and compact sequence packing are adapted from CPython's
# Lib/pprint.py at v3.14.10, originally written by Fred L. Drake, Jr.
# Copyright (c) Python Software Foundation.
# Derived portions are licensed under the PSF License Agreement.

import sys as _sys
from builtins import __clover_runtime_type__ as _runtime_type

__all__ = ["PrettyPrinter", "pformat", "pprint"]


class _Chunks:
    def __init__(self):
        self.parts = []

    def write(self, text):
        self.parts.append(text)


class _DefaultStdout:
    def write(self, text):
        # A stream writes the string payload even for a str subclass.
        print(str.__str__(text), end="")


_default_stdout = _DefaultStdout()


def _container_kind(obj):
    cls = _runtime_type(obj)
    if cls is list or cls is tuple or cls is dict:
        return cls
    for kind in (list, tuple, dict):
        if issubclass(cls, kind):
            if cls.__repr__ is kind.__repr__:
                return kind
            break
    return None


def _is_ancestor(obj, ancestors):
    for active in ancestors:
        if active is obj:
            return True
    return False


def _recursion(obj):
    return "<Recursion on " + _runtime_type(obj).__name__ + ">"


def _item_key(item):
    return item[0]


class PrettyPrinter:
    """Format containers with a configurable width, indent, and depth."""

    def __init__(self, indent=1, width=80, depth=None, stream=None, *,
                 compact=False, sort_dicts=True):
        indent = int(indent)
        width = int(width)
        if indent < 0:
            raise ValueError("indent must be >= 0")
        if depth is not None and depth <= 0:
            raise ValueError("depth must be > 0")
        if not width:
            raise ValueError("width must be != 0")
        self._indent = indent
        self._width = width
        self._depth = depth
        if stream is None:
            stream = getattr(_sys, "stdout", _default_stdout)
        self._stream = stream
        self._compact = bool(compact)
        self._sort_dicts = sort_dicts

    def pformat(self, object):
        """Return the formatted representation without adding a newline."""
        chunks = _Chunks()
        self._format(object, chunks, 0, 0, [], 0)
        return "".join(chunks.parts)

    def pprint(self, object):
        """Write the formatted representation followed by a newline."""
        stream = self._stream
        if stream is not None:
            self._format(object, stream, 0, 0, [], 0)
            stream.write("\n")

    def _dict_items(self, obj):
        if self._sort_dicts:
            return sorted(obj.items(), key=_item_key)
        return obj.items()

    def _flat_repr(self, obj, ancestors, level, kind):
        if kind is None:
            text = repr(obj)
            if not isinstance(text, str):
                raise TypeError("__repr__ must return a string")
            return text

        if kind is dict:
            opening = "{"
            closing = "}"
        elif kind is list:
            opening = "["
            closing = "]"
        else:
            opening = "("
            closing = ",)" if len(obj) == 1 else ")"

        if not obj:
            return opening + closing
        if self._depth is not None and level >= self._depth:
            return opening + "..." + closing
        if _is_ancestor(obj, ancestors):
            return _recursion(obj)

        ancestors.append(obj)
        try:
            parts = []
            next_level = level + 1
            if kind is dict:
                for item in self._dict_items(obj):
                    key = item[0]
                    value = item[1]
                    key_text = self._flat_repr(key, ancestors, next_level,
                                               _container_kind(key))
                    value_text = self._flat_repr(value, ancestors, next_level,
                                                 _container_kind(value))
                    parts.append(": ".join([str(key_text), str(value_text)]))
            else:
                for value in obj:
                    parts.append(self._flat_repr(value, ancestors, next_level,
                                                 _container_kind(value)))
            return opening + ", ".join(parts) + closing
        finally:
            ancestors.pop()

    def _format(self, obj, stream, indent, allowance, ancestors, level):
        if _is_ancestor(obj, ancestors):
            stream.write(_recursion(obj))
            return
        kind = _container_kind(obj)
        # The flat pass applies depth truncation. A flat result that exceeds
        # the width can still be expanded, as in CPython.
        text = self._flat_repr(obj, ancestors, level, kind)
        if len(text) <= self._width - indent - allowance or kind is None:
            stream.write(text)
            return

        ancestors.append(obj)
        try:
            if kind is dict:
                self._format_dict(obj, stream, indent, allowance + 1,
                                  ancestors, level + 1)
            else:
                if kind is list:
                    stream.write("[")
                    closing = "]"
                else:
                    stream.write("(")
                    closing = ",)" if len(obj) == 1 else ")"
                self._format_items(obj, stream, indent, allowance + len(closing),
                                   ancestors, level + 1)
                stream.write(closing)
        finally:
            ancestors.pop()

    def _format_dict(self, obj, stream, indent, allowance, ancestors, level):
        write = stream.write
        write("{")
        if self._indent > 1:
            write("".ljust(self._indent - 1))
        if len(obj):
            items = self._dict_items(obj)
            indent += self._indent
            newline = ",\n" + "".ljust(indent)
            last_index = len(items) - 1
            for indexed in enumerate(items):
                item = indexed[1]
                key = item[0]
                value = item[1]
                last = indexed[0] == last_index
                key_text = self._flat_repr(key, ancestors, level,
                                           _container_kind(key))
                write(key_text)
                write(": ")
                self._format(value, stream, indent + len(key_text) + 2,
                             allowance if last else 1, ancestors, level)
                if not last:
                    write(newline)
        write("}")

    def _format_items(self, obj, stream, indent, allowance, ancestors, level):
        write = stream.write
        indent += self._indent
        if self._indent > 1:
            write("".ljust(self._indent - 1))
        newline = ",\n" + "".ljust(indent)
        delimiter = ""
        max_width = self._width - indent + 1
        width = max_width
        items = iter(obj)
        try:
            following = next(items)
        except StopIteration:
            return

        last = False
        while not last:
            value = following
            # Only the last item needs room for all pending closing tokens.
            try:
                following = next(items)
            except StopIteration:
                last = True
                max_width -= allowance
                width -= allowance
            if self._compact:
                text = self._flat_repr(value, ancestors, level,
                                       _container_kind(value))
                item_width = len(text) + 2
                if width < item_width:
                    width = max_width
                    if delimiter:
                        delimiter = newline
                if width >= item_width:
                    width -= item_width
                    write(delimiter)
                    delimiter = ", "
                    write(text)
                    continue
            write(delimiter)
            delimiter = newline
            self._format(value, stream, indent, allowance if last else 1,
                         ancestors, level)


def pformat(object, indent=1, width=80, depth=None, *,
            compact=False, sort_dicts=True):
    """Return the formatted representation of object."""
    printer = PrettyPrinter(indent=indent, width=width, depth=depth,
                            compact=compact, sort_dicts=sort_dicts)
    return printer.pformat(object)


def pprint(object, stream=None, indent=1, width=80, depth=None, *,
           compact=False, sort_dicts=True):
    """Write the formatted representation of object to a stream."""
    printer = PrettyPrinter(indent=indent, width=width, depth=depth,
                            stream=stream, compact=compact, sort_dicts=sort_dicts)
    printer.pprint(object)
