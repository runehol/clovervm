# Python test set -- os module
#
# Portions adapted from CPython's Lib/test/test_os.py
# at CPython v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import os


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
assert os.path.dirname("/tmp/name") == "/tmp"
assert os.path.basename("/tmp/name") == "name"

cwd = os.getcwd()
assert len(cwd) > 0
assert os.access(cwd, os.F_OK)
assert os.stat(cwd)[0] > 0
assert os.lstat(cwd)[0] > 0
assert os.path.exists(cwd)
assert os.path.isdir(cwd)
assert not os.path.isfile(cwd)
assert os.path.isabs(cwd)
assert os.path.abspath("stdlib").startswith(cwd)

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
os.unsetenv(key)
assert os.getenv(key) is None

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
