import _os
import posixpath as path
import stat as _stat
import sys


name = "posix"
curdir = "."
pardir = ".."
sep = "/"
pathsep = ":"
linesep = "\n"
defpath = "/bin:/usr/bin"
extsep = "."
altsep = None
devnull = "/dev/null"
sys.modules["os.path"] = path

F_OK = _os.F_OK
R_OK = _os.R_OK
W_OK = _os.W_OK
X_OK = _os.X_OK

SEEK_SET = _os.SEEK_SET
SEEK_CUR = _os.SEEK_CUR
SEEK_END = _os.SEEK_END

O_RDONLY = _os.O_RDONLY
O_WRONLY = _os.O_WRONLY
O_RDWR = _os.O_RDWR
O_CREAT = _os.O_CREAT
O_EXCL = _os.O_EXCL
O_TRUNC = _os.O_TRUNC
O_APPEND = _os.O_APPEND

getcwd = _os.getcwd
chdir = _os.chdir
getpid = _os.getpid
getppid = _os.getppid
getuid = _os.getuid
geteuid = _os.geteuid
getgid = _os.getgid
getegid = _os.getegid
getenv = _os.getenv
putenv = _os.putenv
unsetenv = _os.unsetenv
strerror = _os.strerror
system = _os.system
umask = _os.umask
stat = _os.stat
lstat = _os.lstat
access = _os.access
chmod = _os.chmod
rmdir = _os.rmdir
remove = _os.unlink
unlink = _os.unlink
rename = _os.rename
replace = _os.rename


def listdir(path="."):
    return _os.listdir(path)


def mkdir(path, mode=511):
    return _os.mkdir(path, mode)


def makedirs(name, mode=511, exist_ok=False):
    head = _dirname(name)
    if head != "" and head != name and not _exists(head):
        makedirs(head, mode, exist_ok)
    try:
        mkdir(name, mode)
    except ValueError:
        if not exist_ok or not _isdir(name):
            raise


def _exists(path):
    return access(path, F_OK)


def _isdir(path):
    try:
        mode = stat(path)[0]
    except ValueError:
        return False
    return _stat.S_ISDIR(mode)


def _dirname(name):
    return path.dirname(name)
