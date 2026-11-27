"""Wrap or fill a single paragraph of text."""

# The chunk and line-breaking rules are adapted from CPython's Lib/textwrap.py
# at v3.14.10. Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

__all__ = ["TextWrapper", "wrap", "fill"]

_TOKEN_WHITESPACE = " \t\n\r\v\f"
# Token boundaries and the Unicode whitespace trimmed at line edges differ.
_TRIM_WHITESPACE = (
    _TOKEN_WHITESPACE + "\x1c\x1d\x1e\x1f\x85\xa0\u1680"
    + "\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a"
    + "\u2028\u2029\u202f\u205f\u3000"
)
_LOWER = "abcdefghijklmnopqrstuvwxyz"
_LETTER = _LOWER + "ABCDEFGHIJKLMNOPQRSTUVWXYZ_"
_WORD = _LETTER + "0123456789"
_WORD_PUNCTUATION = _WORD + "!\"'&.,?"


def _compound_break(text, hyphen, size):
    # Apply CPython's compound rule with ASCII letter predicates.
    if hyphen < 2 or hyphen + 2 >= size or text[hyphen - 1] not in _LETTER:
        return False
    before = text[hyphen - 2]
    if before not in _LETTER and not (
        before == "-" and hyphen >= 3 and text[hyphen - 3] in _LETTER
    ):
        return False
    after = hyphen + 2
    if text[after] == "-":
        after += 1
    return after < size and text[hyphen + 1] in _LETTER and text[after] in _LETTER


def _scan(text, split_hyphens, whitespace):
    spans = []
    size = len(text)
    start = 0
    pos = 0
    while pos < size:
        character = text[pos]
        if character in whitespace:
            if start < pos:
                spans.append((start, pos))
            start = pos
            pos += 1
            while pos < size and text[pos] in whitespace:
                pos += 1
            spans.append((start, pos))
            start = pos
        elif character == "-" and split_hyphens:
            end = pos + 1
            while end < size and text[end] == "-":
                end += 1
            if end - pos >= 2:
                if pos > 0 and end < size and (
                    text[pos - 1] in _WORD_PUNCTUATION and text[end] in _WORD
                ):
                    if start < pos:
                        spans.append((start, pos))
                    spans.append((pos, end))
                    start = end
            elif _compound_break(text, pos, size):
                spans.append((start, end))
                start = end
            pos = end
        else:
            pos += 1
    if start < size:
        spans.append((start, size))
    return spans


def _sentence_end(text, start, end):
    if text[end - 1] in "\"'":
        end -= 1
    return (end - start >= 2 and text[end - 1] in ".!?"
            and text[end - 2] in _LOWER)


def _fix_sentence_spacing(text, spans):
    # Keep offsets in the expanded text so a forced break can split the new space.
    parts = []
    copied = 0
    shift = 0
    previous = (0, 0)
    for index in range(len(spans)):
        span = spans[index]
        start = span[0]
        end = span[1]
        old_shift = shift
        if (index > 0 and end - start == 1 and text[start] == " "
            and _sentence_end(text, previous[0], previous[1])
        ):
            parts.append(text[copied:end])
            parts.append(" ")
            copied = end
            shift += 1
        if shift:
            spans[index] = (start + old_shift, end + shift)
        previous = span
    if parts:
        parts.append(text[copied:])
        return "".join(parts)
    return text


def _first_nonblank(text, span):
    start = span[0]
    end = span[1]
    while start < end:
        if text[start] not in _TRIM_WHITESPACE:
            return start
        start += 1
    return -1


def _long_word_end(text, start, end, room, break_on_hyphens):
    cut = start + room
    if break_on_hyphens and cut < end:
        hyphen = text.rfind("-", start, cut)
        if hyphen > start:
            first_non_hyphen = start
            while first_non_hyphen < hyphen and text[first_non_hyphen] == "-":
                first_non_hyphen += 1
            if first_non_hyphen < hyphen:
                return hyphen + 1
    return min(cut, end)


class TextWrapper:
    """Wrap a single paragraph using the current settings on this instance."""

    def __init__(self, width=70, initial_indent="", subsequent_indent="",
                 expand_tabs=True, replace_whitespace=True,
                 fix_sentence_endings=False, break_long_words=True,
                 drop_whitespace=True, break_on_hyphens=True, tabsize=8, *,
                 max_lines=None, placeholder=" [...]"):
        self.width = width
        self.initial_indent = initial_indent
        self.subsequent_indent = subsequent_indent
        self.expand_tabs = expand_tabs
        self.replace_whitespace = replace_whitespace
        self.fix_sentence_endings = fix_sentence_endings
        self.break_long_words = break_long_words
        self.drop_whitespace = drop_whitespace
        self.break_on_hyphens = break_on_hyphens
        self.tabsize = tabsize
        self.max_lines = max_lines
        self.placeholder = placeholder

    def wrap(self, text):
        """Return the wrapped lines of text."""
        if not isinstance(text, str):
            raise TypeError("text must be a string")
        if self.expand_tabs:
            text = text.expandtabs(self.tabsize)
        whitespace = _TOKEN_WHITESPACE
        if self.replace_whitespace:
            for character in ("\t", "\n", "\r", "\v", "\f"):
                if character in text:
                    text = text.replace(character, " ")
            whitespace = " "
        spans = _scan(text, self.break_on_hyphens is True, whitespace)
        if self.fix_sentence_endings:
            text = _fix_sentence_spacing(text, spans)
        return self._wrap_spans(text, spans)

    def _wrap_spans(self, text, spans):
        if not isinstance(self.width, int):
            raise TypeError("width must be an integer")
        if self.width <= 0:
            raise ValueError("invalid width " + repr(self.width) + " (must be > 0)")
        if self.max_lines is not None:
            indent = self.subsequent_indent if self.max_lines > 1 else self.initial_indent
            short_placeholder = self.placeholder.lstrip(_TRIM_WHITESPACE)
            if len(indent) + len(short_placeholder) > self.width:
                raise ValueError("placeholder too large for max width")

        lines = []
        next_span = 0
        first_nonblank = -1
        size = len(spans)
        while next_span < size:
            indent = self.subsequent_indent if lines else self.initial_indent
            width = self.width - len(indent)
            if self.drop_whitespace and lines:
                front = spans[next_span]
                # A forced split only advances the start of the pending span.
                # Reuse its first nonblank position until the start passes it.
                if front[0] > first_nonblank:
                    first_nonblank = _first_nonblank(text, front)
                    if first_nonblank < 0:
                        next_span += 1

            line = []
            length = 0
            while next_span < size:
                span = spans[next_span]
                start = span[0]
                end = span[1]
                span_length = end - start
                if length + span_length > width:
                    break
                line.append(span)
                length += span_length
                next_span += 1

            if next_span < size and span_length > width:
                room = 1 if width < 1 else width - length
                if self.break_long_words and room > 0:
                    cut = _long_word_end(text, start, end, room, self.break_on_hyphens)
                    line.append((start, cut))
                    length += cut - start
                    if start == end and (self.drop_whitespace or self.max_lines is None):
                        # A dropped or unlimited empty remainder cannot advance the line limit.
                        next_span += 1
                    else:
                        spans[next_span] = (cut, end)
                elif not line:
                    line.append(span)
                    length += span_length
                    next_span += 1

            if self.drop_whitespace and line and _first_nonblank(text, line[-1]) < 0:
                tail = line.pop()
                length -= tail[1] - tail[0]
            if not line:
                continue

            if (self.max_lines is None or len(lines) + 1 < self.max_lines
                or (next_span == size or (
                    self.drop_whitespace and next_span + 1 == size
                    and _first_nonblank(text, spans[next_span]) < 0
                )) and length <= width
            ):
                lines.append(indent + text[line[0][0]:line[-1][1]])
                continue

            placeholder_length = len(self.placeholder)
            while line:
                if length + placeholder_length <= width and _first_nonblank(text, line[-1]) >= 0:
                    lines.append(indent + text[line[0][0]:line[-1][1]] + self.placeholder)
                    break
                tail = line.pop()
                length -= tail[1] - tail[0]
            else:
                if lines:
                    previous = lines[-1].rstrip(_TRIM_WHITESPACE)
                    if len(previous) + placeholder_length <= self.width:
                        lines[-1] = previous + self.placeholder
                        break
                lines.append(indent + short_placeholder)
            break
        return lines

    def fill(self, text):
        """Return the wrapped lines of text joined by newlines."""
        return "\n".join(self.wrap(text))


def wrap(text, width=70, **kwargs):
    """Return the lines of text wrapped with the supplied settings."""
    return TextWrapper(width=width, **kwargs).wrap(text)


def fill(text, width=70, **kwargs):
    """Return text wrapped with the supplied settings and joined by newlines."""
    return TextWrapper(width=width, **kwargs).fill(text)
