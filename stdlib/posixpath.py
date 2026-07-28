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
        return path
    return join(os.getcwd(), path)
