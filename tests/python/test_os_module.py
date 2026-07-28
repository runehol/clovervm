# Python test set -- os module
#
# Portions adapted from CPython's Lib/test/test_os.py
# at CPython v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import os


def assert_pair(pair, first, second):
    assert pair[0] == first
    assert pair[1] == second


assert os.name == "posix"
assert os.curdir == "."
assert os.pardir == ".."
assert os.sep == "/"
assert os.pathsep == ":"
assert os.linesep == "\n"
assert os.defpath == "/bin:/usr/bin"
assert os.extsep == "."
assert os.altsep is None
assert os.devnull == "/dev/null"
assert os.path.join("a", "b", "c") == "a/b/c"
assert os.path.join("a", "/b", "c") == "/b/c"
assert os.path.split("a/b.txt")[0] == "a"
assert os.path.split("a/b.txt")[1] == "b.txt"
assert_pair(os.path.split("plain"), "", "plain")
assert_pair(os.path.split("/tmp/"), "/tmp", "")
assert_pair(os.path.split("/"), "/", "")
assert_pair(os.path.split("///"), "///", "")
assert_pair(os.path.split("//tmp//name"), "//tmp", "name")
assert_pair(os.path.splitdrive("/tmp/name"), "", "/tmp/name")
assert_pair(os.path.splitext("plain"), "plain", "")
assert_pair(os.path.splitext("plain.txt"), "plain", ".txt")
assert_pair(os.path.splitext(".profile"), ".profile", "")
assert_pair(os.path.splitext("/tmp/archive.tar.gz"), "/tmp/archive.tar", ".gz")
assert_pair(os.path.splitext("/tmp/name."), "/tmp/name", ".")
assert os.path.dirname("/tmp/name") == "/tmp"
assert os.path.basename("/tmp/name") == "name"
assert os.path.normpath("") == "."
assert os.path.normpath(".//a/./b/../c") == "a/c"
assert os.path.normpath("/a//b/..") == "/a"
assert os.path.normpath("//a//b") == "//a/b"
assert os.path.normpath("a/../../b") == "../b"
assert os.path.commonprefix(("/usr/lib", "/usr/local")) == "/usr/l"
assert os.path.commonprefix(("alpha", "beta")) == ""
assert os.path.commonpath(("a/b", "a/c")) == "a"
assert os.path.commonpath(("a", "b")) == ""
assert os.path.commonpath(("/a/b", "/a/c")) == "/a"
assert os.path.relpath("/a/b", "/a/c") == "../b"
assert os.path.relpath("/a/b", "/a/b") == "."
assert os.path.relpath("a/b", ".") == "a/b"

try:
    os.path.commonpath(("a", "/a"))
    mixed_commonpath_raised = False
except ValueError:
    mixed_commonpath_raised = True
assert mixed_commonpath_raised

try:
    os.path.relpath("")
    empty_relpath_raised = False
except ValueError:
    empty_relpath_raised = True
assert empty_relpath_raised

cwd = os.getcwd()
assert len(cwd) > 0
assert os.access(cwd, os.F_OK)
assert os.stat(cwd)[0] > 0
assert os.lstat(cwd)[0] > 0
assert os.path.exists(cwd)
assert os.path.lexists(cwd)
assert os.path.isdir(cwd)
assert not os.path.isfile(cwd)
assert not os.path.islink(cwd)
assert os.path.isabs(cwd)
assert os.path.abspath("stdlib").startswith(cwd)
assert os.path.samefile(cwd, ".")
assert os.path.samestat(os.stat(cwd), os.stat("."))
assert os.path.ismount("/")
assert not os.path.exists("stdlib/not-present")
assert not os.path.lexists("stdlib/not-present")

entries = os.listdir(".")
found_stdlib = False
for entry in entries:
    if entry == "stdlib":
        found_stdlib = True
assert found_stdlib

assert os.getpid() > 0
assert os.getppid() > 0
assert os.getuid() >= 0
assert os.geteuid() >= 0
assert os.getgid() >= 0
assert os.getegid() >= 0

assert len(os.strerror(2)) > 0
assert os.system("true") == 0

key = "CLOVERVM_OS_TEST_VALUE"
os.unsetenv(key)
assert os.getenv(key) is None
os.putenv(key, "present")
assert os.getenv(key) == "present"
assert os.path.expandvars("$CLOVERVM_OS_TEST_VALUE/path") == "present/path"
assert os.path.expandvars("${CLOVERVM_OS_TEST_VALUE}/path") == "present/path"
os.unsetenv(key)
assert os.getenv(key) is None
assert os.path.expandvars("$CLOVERVM_OS_TEST_VALUE/path") == "$CLOVERVM_OS_TEST_VALUE/path"
assert os.path.expandvars("${CLOVERVM_OS_TEST_VALUE}/path") == "${CLOVERVM_OS_TEST_VALUE}/path"
assert os.path.expandvars("$") == "$"
assert os.path.expandvars("${") == "${"

home_key = "HOME"
os.putenv(home_key, "/tmp/clover-home")
assert os.path.expanduser("~") == "/tmp/clover-home"
assert os.path.expanduser("~/work") == "/tmp/clover-home/work"
assert os.path.expanduser("~nosuchuser/work") == "~nosuchuser/work"
assert os.path.expandvars("$HOME/work") == "/tmp/clover-home/work"

base = "build-debug/clovervm-os-test"
nested = base.__add__("/a/b")
parent = base.__add__("/a")
try:
    os.rmdir(nested)
except ValueError:
    pass
try:
    os.rmdir(parent)
except ValueError:
    pass
try:
    os.rmdir(base)
except ValueError:
    pass

os.makedirs(nested)
assert os.access(nested, os.F_OK)
assert os._isdir(nested)
assert os.path.isdir(nested)
os.rmdir(nested)
os.rmdir(parent)
os.rmdir(base)
