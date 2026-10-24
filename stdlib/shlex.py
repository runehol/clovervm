"""String-based shell-style splitting and quoting.

Both POSIX and non-POSIX split modes are supported. Streams, source inclusion,
and the configurable shlex class are intentionally omitted. Unlike CPython's
stream-capable split, non-string input (other than None) raises TypeError.
join accepts iterables supported by the VM; string iteration is not yet available.
"""
# Scanner semantics and quoting adapted from CPython Lib/shlex.py, v3.14.5.
# Original module by Eric S. Raymond; POSIX support by Gustavo Niemeyer.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

from builtins import __clover_runtime_type__ as _runtime_type


__all__ = ["split", "quote", "join"]

_safe_chars = (
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    + "0123456789_@%+=:,./-"
)


def split(s, comments=False, posix=True):
    """Split a string using shell-like syntax, optionally recognizing comments."""
    if s is None:
        raise ValueError("s argument must not be None")
    if not isinstance(s, str):
        raise TypeError("s argument must be a string")

    result = []
    token = []
    started = False
    state = " "
    escaped_state = "a"
    idx = 0
    # Like StringIO(s), scan the payload without subclass length/index overrides.
    length = str.__len__(s)
    while idx < length:
        ch = str.__getitem__(s, idx)
        idx += 1
        if state == "\\":
            # Inside double quotes, only quote and backslash lose the escape.
            if escaped_state == '"' and ch != '"' and ch != "\\":
                token.append("\\")
            token.append(ch)
            state = escaped_state
        elif state == "'" or state == '"':
            if ch == state:
                if posix:
                    state = "a"
                else:
                    token.append(ch)
                    result.append("".join(token))
                    token = []
                    started = False
                    state = " "
            elif posix and ch == "\\" and state == '"':
                escaped_state = state
                state = "\\"
            else:
                token.append(ch)
        elif ch in " \t\r\n":
            if started:
                result.append("".join(token))
                token = []
                started = False
            state = " "
        elif comments and ch == "#":
            # StringIO.readline() consumes through LF, not through bare CR.
            while idx < length and str.__getitem__(s, idx) != "\n":
                idx += 1
            if idx < length:
                idx += 1
            if posix:
                if started:
                    result.append("".join(token))
                    token = []
                    started = False
                state = " "
            # Non-POSIX comments leave word state intact, even across LF.
        elif posix and ch == "\\":
            started = True
            escaped_state = "a"
            state = "\\"
        elif (ch == "'" or ch == '"') and (posix or state == " "):
            started = True
            state = ch
            if not posix:
                token.append(ch)
        else:
            started = True
            token.append(ch)
            state = "a"

    if state == "\\":
        raise ValueError("No escaped character")
    if state == "'" or state == '"':
        raise ValueError("No closing quotation")
    if started:
        result.append("".join(token))
    return result


def quote(s):
    """Return a shell-escaped version of a string."""
    if not s:
        return "''"
    if not isinstance(s, str):
        raise TypeError("expected string object, got " + repr(_runtime_type(s).__name__))
    # Inspect the payload: subclass length/index overrides can hide metacharacters.
    length = str.__len__(s)
    idx = 0
    while idx < length:
        if str.__getitem__(s, idx) not in _safe_chars:
            return "'" + s.replace("'", "'\"'\"'") + "'"
        idx += 1
    return s


def join(split_command):
    """Return a shell-escaped string from an iterable of arguments."""
    quoted = []
    for arg in split_command:
        quoted.append(quote(arg))
    return " ".join(quoted)
