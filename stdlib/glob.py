"""Filename globbing utility."""

import fnmatch
import os


def glob(pathname, *, root_dir=None, dir_fd=None, recursive=False, include_hidden=False):
    """Return a list of paths matching a pathname pattern."""
    if root_dir is not None or dir_fd is not None:
        raise UnimplementedError
    return _glob(pathname, recursive, include_hidden)


def iglob(pathname, *, root_dir=None, dir_fd=None, recursive=False, include_hidden=False):
    """Return an iterator over paths matching a pathname pattern."""
    return iter(
        glob(
            pathname,
            root_dir=root_dir,
            dir_fd=dir_fd,
            recursive=recursive,
            include_hidden=include_hidden,
        )
    )


def has_magic(s):
    idx = 0
    while idx < len(s):
        ch = s[idx]
        if ch == "*" or ch == "?" or ch == "[":
            return True
        idx += 1
    return False


def escape(pathname):
    result = ""
    idx = 0
    while idx < len(pathname):
        ch = pathname[idx]
        if ch == "*" or ch == "?" or ch == "[":
            result = result.__add__("[").__add__(ch).__add__("]")
        else:
            result = result.__add__(ch)
        idx += 1
    return result


def glob0(dirname, pattern):
    return _glob0(dirname, pattern)


def glob1(dirname, pattern):
    return _glob1(dirname, pattern, False, False)


def _glob(pathname, recursive, include_hidden):
    dirname, basename = os.path.split(pathname)
    if not has_magic(pathname):
        if basename:
            if os.path.exists(pathname):
                return [pathname]
        elif os.path.isdir(dirname):
            return [pathname]
        return []

    if recursive and basename == "**":
        return _recursive_dirs(dirname, include_hidden)

    if dirname != "" and has_magic(dirname):
        dirs = _glob(dirname, recursive, include_hidden)
    else:
        dirs = [dirname]

    result = []
    for dirname in dirs:
        if has_magic(basename):
            matches = _glob1(dirname, basename, recursive, include_hidden)
            for name in matches:
                result.append(_join(dirname, name))
        else:
            matches = _glob0(dirname, basename)
            for name in matches:
                result.append(_join(dirname, name))
    return result


def _glob0(dirname, basename):
    pathname = _join(dirname, basename)
    if basename:
        if os.path.exists(pathname):
            return [basename]
    elif os.path.isdir(dirname):
        return [basename]
    return []


def _glob1(dirname, pattern, recursive, include_hidden):
    if recursive and pattern == "**":
        return _recursive_dirs(dirname, include_hidden)

    names = _listdir(dirname)
    result = []
    pattern_hidden = _ishidden(pattern)
    for name in names:
        if (include_hidden or pattern_hidden or not _ishidden(name)) and fnmatch.fnmatchcase(
            name, pattern
        ):
            result.append(name)
    return result


def _recursive_dirs(dirname, include_hidden):
    result = [dirname]
    names = _listdir(dirname)
    for name in names:
        if include_hidden or not _ishidden(name):
            path = _join(dirname, name)
            if os.path.isdir(path):
                children = _recursive_dirs(path, include_hidden)
                for child in children:
                    result.append(child)
    return result


def _listdir(dirname):
    if dirname == "":
        dirname = "."
    try:
        return os.listdir(dirname)
    except ValueError:
        return ()


def _join(dirname, basename):
    if dirname == "" or basename == "":
        return dirname.__add__(basename)
    return os.path.join(dirname, basename)


def _ishidden(path):
    return len(path) > 0 and path[0] == "."


def translate(pat, *, recursive=False, include_hidden=False, seps=None):
    """Translate a pathname with shell wildcards to a regular expression."""
    if seps is not None:
        raise UnimplementedError
    result = ""
    part = ""
    idx = 0
    while idx < len(pat):
        ch = pat[idx]
        if ch == os.path.sep:
            result = result.__add__(_translate_part(part, recursive, include_hidden))
            result = result.__add__("/")
            part = ""
        else:
            part = part.__add__(ch)
        idx += 1
    result = result.__add__(_translate_part(part, recursive, include_hidden))
    return "(?s:".__add__(result).__add__(")\\z")


def _translate_part(part, recursive, include_hidden):
    if recursive and part == "**":
        return "(?:[^/.][^/]*/)*"
    translated = ""
    if not include_hidden and len(part) > 0 and (part[0] == "*" or part[0] == "?"):
        translated = "(?!\\.)"
    idx = 0
    while idx < len(part):
        ch = part[idx]
        if ch == "*":
            while idx + 1 < len(part) and part[idx + 1] == "*":
                idx += 1
            translated = translated.__add__("[^/]*")
        elif ch == "?":
            translated = translated.__add__("[^/]")
        elif ch == "[":
            end = _class_end(part, idx)
            if end < 0:
                translated = translated.__add__("\\[")
            else:
                translated = translated.__add__(_translate_class(part, idx, end))
                idx = end
        else:
            translated = translated.__add__(_regex_escape(ch))
        idx += 1
    return translated


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


def _translate_class(pat, start, end):
    idx = start + 1
    negate = False
    if idx < end and pat[idx] == "!":
        negate = True
        idx += 1

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
        return "[^/]"
    return "[".__add__(stuff).__add__("]")


def _regex_escape(ch):
    if ch in "\\.^$+{}[]|()":
        return "\\".__add__(ch)
    return ch


__all__ = ["glob", "iglob", "escape", "translate"]
