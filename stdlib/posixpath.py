import os
import stat


curdir = "."
pardir = ".."
sep = "/"
pathsep = ":"
defpath = "/bin:/usr/bin"
extsep = "."
altsep = None
devnull = "/dev/null"


def _all_sep(path):
    return len(path) > 0 and path.count(sep) == len(path)


def isabs(path):
    return path.startswith(sep)


def normcase(path):
    return path


def _path_parts(path):
    normalized = normpath(path)
    stripped = normalized.strip(sep)
    if stripped == "":
        return []
    return stripped.split(sep)


def _prefix(parts, n):
    result = []
    idx = 0
    while idx < n:
        result.append(parts[idx])
        idx = idx + 1
    return result


def join(path, *paths):
    result = path
    for item in paths:
        if item.startswith(sep):
            result = item
        elif result == "" or result.endswith(sep):
            result = result + item
        else:
            result = result + sep + item
    return result


def split(path):
    sep_idx = path.rfind(sep)
    if sep_idx < 0:
        return ("", path)

    head = path[:sep_idx + 1]
    tail = path[sep_idx + 1:]
    if not _all_sep(head):
        head = head.rstrip(sep)
    return (head, tail)


def splitdrive(path):
    return ("", path)


def splitext(path):
    sep_idx = path.rfind(sep)
    dot_idx = path.rfind(extsep)
    filename_start = sep_idx + 1

    if dot_idx <= filename_start:
        return (path, "")

    idx = filename_start
    while idx < dot_idx and path[idx] == extsep:
        idx = idx + 1
    if idx == dot_idx:
        return (path, "")

    return (path[:dot_idx], path[dot_idx:])


def dirname(path):
    return split(path)[0]


def basename(path):
    return split(path)[1]


def commonprefix(paths):
    if len(paths) == 0:
        return ""
    prefix = paths[0]
    path_idx = 1
    while path_idx < len(paths):
        path = paths[path_idx]
        char_idx = 0
        limit = len(prefix)
        if len(path) < limit:
            limit = len(path)
        while char_idx < limit and prefix[char_idx] == path[char_idx]:
            char_idx = char_idx + 1
        prefix = prefix[:char_idx]
        path_idx = path_idx + 1
    return prefix


def commonpath(paths):
    if len(paths) == 0:
        raise ValueError

    first = paths[0]
    absolute = isabs(first)
    common = _path_parts(first)

    path_idx = 1
    while path_idx < len(paths):
        path = paths[path_idx]
        if isabs(path) != absolute:
            raise ValueError
        parts = _path_parts(path)
        part_idx = 0
        limit = len(common)
        if len(parts) < limit:
            limit = len(parts)
        while part_idx < limit and common[part_idx] == parts[part_idx]:
            part_idx = part_idx + 1
        common = _prefix(common, part_idx)
        path_idx = path_idx + 1

    joined = sep.join(common)
    if absolute:
        return sep + joined
    return joined


def exists(path):
    return os.access(path, os.F_OK)


def isdir(path):
    try:
        mode = os.stat(path)[0]
    except ValueError:
        return False
    return stat.S_ISDIR(mode)


def isfile(path):
    try:
        mode = os.stat(path)[0]
    except ValueError:
        return False
    return stat.S_ISREG(mode)


def abspath(path):
    if isabs(path):
        return normpath(path)
    return normpath(join(os.getcwd(), path))


def normpath(path):
    if path == "":
        return curdir

    initial_slashes = path.startswith(sep)
    if (
        initial_slashes
        and path.startswith(sep + sep)
        and not path.startswith(sep + sep + sep)
    ):
        initial_slashes = 2

    new_comps = []
    comps = path.split(sep)
    for comp in comps:
        if comp == "" or comp == curdir:
            pass
        elif (
            comp != pardir
            or (not initial_slashes and len(new_comps) == 0)
            or (len(new_comps) > 0 and new_comps[-1] == pardir)
        ):
            new_comps.append(comp)
        elif len(new_comps) > 0:
            new_comps.pop()

    result = sep.join(new_comps)
    if initial_slashes:
        result = sep + result
    if initial_slashes == 2:
        result = sep + result
    if result == "":
        return curdir
    return result


def relpath(path, start=curdir):
    if path == "":
        raise ValueError

    start_parts = _path_parts(abspath(start))
    path_parts = _path_parts(abspath(path))
    idx = 0
    limit = len(start_parts)
    if len(path_parts) < limit:
        limit = len(path_parts)
    while idx < limit and start_parts[idx] == path_parts[idx]:
        idx = idx + 1

    result_parts = []
    start_idx = idx
    while start_idx < len(start_parts):
        result_parts.append(pardir)
        start_idx = start_idx + 1

    path_idx = idx
    while path_idx < len(path_parts):
        result_parts.append(path_parts[path_idx])
        path_idx = path_idx + 1

    if len(result_parts) == 0:
        return curdir
    return sep.join(result_parts)
