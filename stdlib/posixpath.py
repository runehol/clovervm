import os
import _os
import stat


curdir = "."
pardir = ".."
sep = "/"
pathsep = ":"
defpath = "/bin:/usr/bin"
extsep = "."
altsep = None
devnull = "/dev/null"


def _concat(left, right):
    return left.__add__(right)


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
            result = _concat(result, item)
        else:
            result = _concat(_concat(result, sep), item)
    return result


def split(path):
    return _os._path_split(path)


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
