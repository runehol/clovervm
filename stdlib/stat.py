"""Constants and helpers for interpreting os.stat() mode values.

This module covers the POSIX mode helpers that CloverVM can support with its
current plain-tuple ``os.stat`` result. Platform-specific flag constants are
left out until the corresponding OS surface is exposed.
"""


ST_MODE = 0
ST_INO = 1
ST_DEV = 2
ST_NLINK = 3
ST_UID = 4
ST_GID = 5
ST_SIZE = 6
ST_ATIME = 7
ST_MTIME = 8
ST_CTIME = 9

S_IFMT_MASK = 0xF000
S_IMODE_MASK = 0x0FFF

S_IFDIR = 0x4000
S_IFCHR = 0x2000
S_IFBLK = 0x6000
S_IFREG = 0x8000
S_IFIFO = 0x1000
S_IFLNK = 0xA000
S_IFSOCK = 0xC000
S_IFDOOR = 0
S_IFPORT = 0
S_IFWHT = 0xE000

S_ISUID = 0x0800
S_ISGID = 0x0400
S_ISVTX = 0x0200

S_IRWXU = 0x01C0
S_IRUSR = 0x0100
S_IWUSR = 0x0080
S_IXUSR = 0x0040
S_IRWXG = 0x0038
S_IRGRP = 0x0020
S_IWGRP = 0x0010
S_IXGRP = 0x0008
S_IRWXO = 0x0007
S_IROTH = 0x0004
S_IWOTH = 0x0002
S_IXOTH = 0x0001

S_IREAD = S_IRUSR
S_IWRITE = S_IWUSR
S_IEXEC = S_IXUSR
S_ENFMT = S_ISGID


def S_IMODE(mode):
    return mode & S_IMODE_MASK


def S_IFMT(mode):
    return mode & S_IFMT_MASK


def S_ISDIR(mode):
    return S_IFMT(mode) == S_IFDIR


def S_ISCHR(mode):
    return S_IFMT(mode) == S_IFCHR


def S_ISBLK(mode):
    return S_IFMT(mode) == S_IFBLK


def S_ISREG(mode):
    return S_IFMT(mode) == S_IFREG


def S_ISFIFO(mode):
    return S_IFMT(mode) == S_IFIFO


def S_ISLNK(mode):
    return S_IFMT(mode) == S_IFLNK


def S_ISSOCK(mode):
    return S_IFMT(mode) == S_IFSOCK


def S_ISDOOR(mode):
    return False


def S_ISPORT(mode):
    return False


def S_ISWHT(mode):
    return S_IFMT(mode) == S_IFWHT


def _file_type_char(mode):
    kind = S_IFMT(mode)
    if kind == S_IFDIR:
        return "d"
    if kind == S_IFCHR:
        return "c"
    if kind == S_IFBLK:
        return "b"
    if kind == S_IFREG:
        return "-"
    if kind == S_IFIFO:
        return "p"
    if kind == S_IFLNK:
        return "l"
    if kind == S_IFSOCK:
        return "s"
    if kind == S_IFWHT:
        return "w"
    return "?"


def _rwx_char(mode, bit, ch):
    if mode & bit:
        return ch
    return "-"


def _exec_char(mode, exec_bit, special_bit, exec_ch, no_exec_ch):
    if mode & special_bit:
        if mode & exec_bit:
            return exec_ch
        return no_exec_ch
    return _rwx_char(mode, exec_bit, "x")


def filemode(mode):
    """Convert a file's mode to a string of the form '-rwxrwxrwx'."""
    return (
        _file_type_char(mode)
        .__add__(_rwx_char(mode, S_IRUSR, "r"))
        .__add__(_rwx_char(mode, S_IWUSR, "w"))
        .__add__(_exec_char(mode, S_IXUSR, S_ISUID, "s", "S"))
        .__add__(_rwx_char(mode, S_IRGRP, "r"))
        .__add__(_rwx_char(mode, S_IWGRP, "w"))
        .__add__(_exec_char(mode, S_IXGRP, S_ISGID, "s", "S"))
        .__add__(_rwx_char(mode, S_IROTH, "r"))
        .__add__(_rwx_char(mode, S_IWOTH, "w"))
        .__add__(_exec_char(mode, S_IXOTH, S_ISVTX, "t", "T"))
    )


__all__ = (
    "ST_MODE",
    "ST_INO",
    "ST_DEV",
    "ST_NLINK",
    "ST_UID",
    "ST_GID",
    "ST_SIZE",
    "ST_ATIME",
    "ST_MTIME",
    "ST_CTIME",
    "S_IMODE",
    "S_IFMT",
    "S_IFDIR",
    "S_IFCHR",
    "S_IFBLK",
    "S_IFREG",
    "S_IFIFO",
    "S_IFLNK",
    "S_IFSOCK",
    "S_IFDOOR",
    "S_IFPORT",
    "S_IFWHT",
    "S_ISDIR",
    "S_ISCHR",
    "S_ISBLK",
    "S_ISREG",
    "S_ISFIFO",
    "S_ISLNK",
    "S_ISSOCK",
    "S_ISDOOR",
    "S_ISPORT",
    "S_ISWHT",
    "S_ISUID",
    "S_ISGID",
    "S_ISVTX",
    "S_IRWXU",
    "S_IRUSR",
    "S_IWUSR",
    "S_IXUSR",
    "S_IRWXG",
    "S_IRGRP",
    "S_IWGRP",
    "S_IXGRP",
    "S_IRWXO",
    "S_IROTH",
    "S_IWOTH",
    "S_IXOTH",
    "S_IREAD",
    "S_IWRITE",
    "S_IEXEC",
    "S_ENFMT",
    "filemode",
)
