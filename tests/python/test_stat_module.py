# Python test set -- stat module
#
# Surface selected from Python 3.14's stat module help and POSIX constants.
# This file does not copy CPython test code.

import os
import stat


assert stat.ST_MODE == 0
assert stat.ST_INO == 1
assert stat.ST_DEV == 2
assert stat.ST_NLINK == 3
assert stat.ST_UID == 4
assert stat.ST_GID == 5
assert stat.ST_SIZE == 6
assert stat.ST_ATIME == 7
assert stat.ST_MTIME == 8
assert stat.ST_CTIME == 9

assert stat.S_IMODE(33261) == 493
assert stat.S_IFMT(33261) == stat.S_IFREG
assert stat.S_IFMT(16877) == stat.S_IFDIR

assert stat.S_ISREG(33261)
assert not stat.S_ISDIR(33261)
assert stat.S_ISDIR(16877)
assert not stat.S_ISREG(16877)
assert stat.S_ISCHR(stat.S_IFCHR)
assert stat.S_ISBLK(stat.S_IFBLK)
assert stat.S_ISFIFO(stat.S_IFIFO)
assert stat.S_ISLNK(stat.S_IFLNK)
assert stat.S_ISSOCK(stat.S_IFSOCK)
assert not stat.S_ISDOOR(stat.S_IFDIR)
assert not stat.S_ISPORT(stat.S_IFDIR)
assert stat.S_ISWHT(stat.S_IFWHT)

assert stat.filemode(33261) == "-rwxr-xr-x"
assert stat.filemode(33188) == "-rw-r--r--"
assert stat.filemode(16895) == "drwxrwxrwx"
assert stat.filemode(41471) == "lrwxrwxrwx"
assert stat.filemode(49663) == "srwxrwxrwx"
assert stat.filemode(2048) == "?--S------"
assert stat.filemode(1024) == "?-----S---"
assert stat.filemode(512) == "?--------T"
assert stat.filemode(61951) == "?rwxrwxrwx"

cwd_mode = os.stat(".")[stat.ST_MODE]
assert stat.S_ISDIR(cwd_mode)
assert os.path.isdir(".")
assert not os.path.isfile(".")

assert stat.__all__[0] == "ST_MODE"
assert stat.__all__[10] == "S_IMODE"
assert stat.__all__[51] == "filemode"
