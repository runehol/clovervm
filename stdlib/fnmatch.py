"""Unix shell-style filename matching.

This module uses a direct matcher instead of translating through ``re`` because
CloverVM does not provide regular expressions yet.
"""

import os


def fnmatch(name, pat):
    """Test whether FILENAME matches PATTERN after platform case normalization."""
    return fnmatchcase(os.path.normcase(name), os.path.normcase(pat))


def fnmatchcase(name, pat):
    """Test whether FILENAME matches PATTERN, including case."""
    return _match_at(name, pat, 0, 0)


def filter(names, pat):
    """Construct a list from those elements of NAMES that match PAT."""
    result = []
    pat = os.path.normcase(pat)
    for name in names:
        if fnmatchcase(os.path.normcase(name), pat):
            result.append(name)
    return result


def filterfalse(names, pat):
    """Construct a list from those elements of NAMES that do not match PAT."""
    result = []
    pat = os.path.normcase(pat)
    for name in names:
        if not fnmatchcase(os.path.normcase(name), pat):
            result.append(name)
    return result


def _match_at(name, pat, name_idx, pat_idx):
    name_len = len(name)
    pat_len = len(pat)
    while pat_idx < pat_len:
        pat_ch = pat[pat_idx]
        if pat_ch == "*":
            while pat_idx + 1 < pat_len and pat[pat_idx + 1] == "*":
                pat_idx += 1
            pat_idx += 1
            if pat_idx == pat_len:
                return True
            while name_idx <= name_len:
                if _match_at(name, pat, name_idx, pat_idx):
                    return True
                if name_idx == name_len:
                    return False
                name_idx += 1
            return False

        if name_idx >= name_len:
            return False

        if pat_ch == "?":
            name_idx += 1
            pat_idx += 1
        elif pat_ch == "[":
            end = _class_end(pat, pat_idx)
            if end < 0:
                if name[name_idx] != "[":
                    return False
                name_idx += 1
                pat_idx += 1
            else:
                if not _class_matches(name[name_idx], pat, pat_idx, end):
                    return False
                name_idx += 1
                pat_idx = end + 1
        else:
            if name[name_idx] != pat_ch:
                return False
            name_idx += 1
            pat_idx += 1
    return name_idx == name_len


def _class_end(pat, start):
    pat_len = len(pat)
    idx = start + 1
    if idx < pat_len and pat[idx] == "!":
        idx += 1
    if idx < pat_len and pat[idx] == "]":
        idx += 1
    while idx < pat_len and pat[idx] != "]":
        idx += 1
    if idx >= pat_len:
        return -1
    return idx


def _class_matches(ch, pat, start, end):
    idx = start + 1
    negate = False
    if idx < end and pat[idx] == "!":
        negate = True
        idx += 1

    matched = False
    if idx < end and pat[idx] == "]":
        if ch == "]":
            matched = True
        idx += 1

    while idx < end:
        first = pat[idx]
        if idx + 2 < end and pat[idx + 1] == "-":
            last = pat[idx + 2]
            if first <= last and first <= ch and ch <= last:
                matched = True
            idx += 3
        else:
            if ch == first:
                matched = True
            idx += 1

    if negate:
        return not matched
    return matched


def translate(pat):
    """Translate a shell PATTERN to a regular expression string."""
    result = ""
    idx = 0
    pat_len = len(pat)
    while idx < pat_len:
        ch = pat[idx]
        if ch == "*":
            while idx + 1 < pat_len and pat[idx + 1] == "*":
                idx += 1
            result = result.__add__(".*")
        elif ch == "?":
            result = result.__add__(".")
        elif ch == "[":
            end = _class_end(pat, idx)
            if end < 0:
                result = result.__add__("\\[")
            else:
                result = result.__add__(_translate_class(pat, idx, end))
                idx = end
        else:
            result = result.__add__(_regex_escape(ch))
        idx += 1
    return "(?s:".__add__(result).__add__(")\\z")


def _translate_class(pat, start, end):
    idx = start + 1
    negate = False
    if idx < end and pat[idx] == "!":
        negate = True
        idx += 1

    if idx == end:
        return "\\[\\]"

    stuff = ""
    if negate:
        stuff = "^"

    if idx < end and pat[idx] == "]":
        stuff = stuff.__add__("\\]")
        idx += 1

    previous = None
    while idx < end:
        ch = pat[idx]
        if ch == "\\":
            stuff = stuff.__add__("\\\\")
        elif ch == "-" and (idx == start + 1 or idx == end - 1):
            stuff = stuff.__add__("\\-")
        elif ch == "-" and previous is not None and idx + 1 < end:
            next_ch = pat[idx + 1]
            if previous > next_ch:
                return "(?!)"
            stuff = stuff.__add__("-")
        elif ch == "[":
            stuff = stuff.__add__("\\[")
        elif ch == "^":
            stuff = stuff.__add__("\\^")
        else:
            stuff = stuff.__add__(ch)
        previous = ch
        idx += 1

    if stuff == "^":
        return "."
    return "[".__add__(stuff).__add__("]")


def _regex_escape(ch):
    if ch in "\\.^$+{}[]|()":
        return "\\".__add__(ch)
    return ch


__all__ = ["filter", "filterfalse", "fnmatch", "fnmatchcase", "translate"]
