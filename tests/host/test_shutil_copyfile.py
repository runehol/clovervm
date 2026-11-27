"""Compare POSIX copy fixtures with CPython 3.14.10 and inject raw I/O faults.

Run with python3.14 tests/host/test_shutil_copyfile.py [path/to/clovervm].
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


ALIASES = """\
import io
import os
import shutil
import stat


def raises(expected, fn, *args, **kwargs):
    try:
        fn(*args, **kwargs)
    except expected as error:
        return error
    assert False


def read(path):
    with io.FileIO(path, "rb") as stream:
        return stream.readall()


for alias in ("./src", "hard", "link"):
    for follow in (True, False):
        raises(shutil.SameFileError, shutil.copyfile, "src", alias,
               follow_symlinks=follow)
raises(shutil.SameFileError, shutil.copyfile, "link", "hard", follow_symlinks=False)
assert read("src") == b"contents"
for endpoint in ("fifo", "fifo-link"):
    raises(shutil.SpecialFileError, shutil.copyfile, endpoint, "absent",
           follow_symlinks=False)
    raises(shutil.SpecialFileError, shutil.copyfile, "src", endpoint)
raises(shutil.SameFileError, shutil.copyfile, "fifo", "fifo-link")
assert not os.path.lexists("absent")


class DiagnosticText(str):
    def __add__(self, suffix):
        raise AssertionError("diagnostic invoked __add__")

    def __radd__(self, prefix):
        raise AssertionError("diagnostic invoked __radd__")


class ReportedPath(DiagnosticText):
    def __str__(self):
        return DiagnosticText("reported path")

    def __repr__(self):
        return DiagnosticText("path repr")


error = raises(shutil.SpecialFileError, shutil.copyfile, ReportedPath("fifo"), "absent")
assert str(error) == "`reported path` is a named pipe"
assert not os.path.lexists("absent")
label = ReportedPath("src")
error = raises(shutil.SameFileError, shutil.copyfile, label, "src")
assert str(error) == "path repr and 'src' are the same file"
error = raises(shutil.SameFileError, shutil.copyfile, "src", label)
assert str(error) == "'src' and path repr are the same file"
assert read("src") == b"contents"

# Inject the destination-open error so this branch is tested on every POSIX host.
uses_builtin_open = hasattr(shutil, "_HAS_FCOPYFILE")
if uses_builtin_open:
    original_open = open
else:
    original_open = io.FileIO


def fail_directory_open(path, mode):
    if mode == "wb":
        raise IsADirectoryError("injected destination open failure")
    return original_open(path, mode)


if uses_builtin_open:
    shutil.open = fail_directory_open
else:
    io.FileIO = fail_directory_open
try:
    error = raises(FileNotFoundError, shutil.copyfile, "src", ReportedPath("absent"))
    assert str(error) == "Directory does not exist: reported path"
finally:
    if uses_builtin_open:
        del shutil.open
    else:
        io.FileIO = original_open


class SourcePath(str):
    def startswith(self, prefix):
        raise AssertionError("join received the source subclass")


class InvalidComponent:
    def startswith(self, prefix):
        return False

    def __radd__(self, prefix):
        return "sibling"


class InvalidBasename(str):
    def __getitem__(self, key):
        return InvalidComponent()


class ReflectedComponent(str):
    def __radd__(self, prefix):
        if prefix == "/":
            return "/safe"
        return "sibling"


class ReflectedBasename(str):
    def __getitem__(self, key):
        return ReflectedComponent("component")


class DirectoryPath(str):
    def __eq__(self, other):
        raise AssertionError("join used equality instead of truth testing")

    def __iadd__(self, suffix):
        assert suffix == "/part"
        return "in-place result"


assert os.path.join(DirectoryPath("dir"), "part") == "in-place result"
raises(TypeError, shutil.copy, InvalidBasename("src"), "dir")
assert read("sibling") == b"sentinel"
assert shutil.copy(ReflectedBasename("src"), "dir") == "dir/safe"
assert read("dir/safe") == b"contents"
assert read("sibling") == b"sentinel"

assert shutil.copyfile("link", "through-link") == "through-link"
assert not os.path.islink("through-link") and read("through-link") == b"contents"
assert shutil.copyfile("src", "dangling-dst", follow_symlinks=False) == "dangling-dst"
assert os.path.islink("dangling-dst") and read("target") == b"contents"
assert shutil.copy("src", "dir-link") == "dir-link/src"
assert stat.S_IMODE(os.stat("dir/src")[stat.ST_MODE]) == 489  # 0o751
os.unlink("dir/src")
assert shutil.copy(SourcePath("src"), "dir") == "dir/src"
assert read("dir/src") == b"contents"
raises(FileNotFoundError, shutil.copyfile, "dangling-src", "absent")

for paths in (("link", "new-link"), ("dangling-src", "new-dangling")):
    source = paths[0]
    dest = paths[1]
    try:
        assert shutil.copyfile(source, dest, follow_symlinks=False) == dest
    except NotImplementedError:
        assert not os.path.lexists(dest)
        print("no-follow unavailable")
    else:
        assert os.path.islink(dest)
        print("link recreated")


class PathLike:
    def __fspath__(self):
        return "pathlike-dst"


for dest in (b"bytes-dst", PathLike()):
    try:
        assert shutil.copyfile("src", dest) is dest
    except TypeError:
        print("string paths only")
    else:
        assert read(dest) == b"contents"
        print("converted path")
"""


RAW_FAILURES = """\
import io
import shutil


def read(path):
    with original_file(path, "rb") as stream:
        return stream.readall()


failure = OSError("injected")
original_file = io.FileIO
original_writer = io.BufferedWriter
opened = []
write_counts = []
fail_at = ""


class ShortWriter(original_file):
    def write(self, data):
        write_counts.append(len(data))
        if fail_at == "flush" and len(write_counts) == 2:
            raise failure
        return original_file.write(self, data[:2])


def tracked_file(path, mode):
    if mode == "wb" and fail_at == "open":
        raise failure
    if mode == "wb":
        stream = ShortWriter(path, mode)
    else:
        stream = original_file(path, mode)
    opened.append(stream)
    return stream


def fail_writer(raw):
    assert raw is opened[1]
    raise failure


io.FileIO = tracked_file
try:
    assert shutil.copyfile("src", "short-writes") == "short-writes"
    assert write_counts == [8, 6, 4, 2]
    assert read("short-writes") == b"contents"
    assert len(opened) == 2 and opened[0].closed and opened[1].closed

    for fail_at in ("open", "wrapper", "flush"):
        opened.clear()
        write_counts.clear()
        if fail_at == "wrapper":
            io.BufferedWriter = fail_writer
        else:
            io.BufferedWriter = original_writer
        try:
            shutil.copyfile("src", fail_at)
        except OSError as error:
            assert error is failure
        else:
            assert False
        if fail_at == "open":
            assert len(opened) == 1
        else:
            assert len(opened) == 2 and opened[1].closed
        assert opened[0].closed
    assert write_counts == [8, 6]
    assert read("wrapper") == b""
    assert read("flush") == b"co"
finally:
    io.BufferedWriter = original_writer
    io.FileIO = original_file
"""


def compare(executable, vm, shared_test):
    with tempfile.TemporaryDirectory(prefix="clover-shutil-oracle-") as tmp:
        root = Path(tmp)
        source = root / "src"
        source.write_bytes(b"contents")
        source.chmod(0o751)
        (root / "sibling").write_bytes(b"sentinel")
        os.link(source, root / "hard")
        os.symlink("src", root / "link")
        os.symlink("missing", root / "dangling-src")
        os.symlink("target", root / "dangling-dst")
        os.mkfifo(root / "fifo", 0o600)
        os.symlink("fifo", root / "fifo-link")
        (root / "dir").mkdir()
        os.symlink("dir", root / "dir-link")
        subprocess.run([executable, shared_test], cwd=root, check=True, timeout=30)

        script = root / "aliases.py"
        script.write_text(ALIASES)
        result = subprocess.run([executable, script], cwd=root, check=True, timeout=10,
                                stdout=subprocess.PIPE, text=True)
        if vm:
            assert result.stdout.splitlines() == ["no-follow unavailable"] * 2 + ["string paths only"] * 2
            script = root / "raw_failures.py"
            script.write_text(RAW_FAILURES)
            subprocess.run([executable, script], cwd=root, check=True, timeout=10)
        else:
            assert result.stdout.splitlines() == ["link recreated"] * 2 + ["converted path"] * 2
            assert os.readlink(root / "new-link") == "src"
            assert os.readlink(root / "new-dangling") == "missing"


if __name__ == "__main__":
    assert sys.implementation.name == "cpython" and sys.version_info[:3] == (3, 14, 10)
    repo = Path(__file__).resolve().parents[2]
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else repo / "build-debug/src/clovervm"
    shared = repo / "tests/python/test_shutil_copyfile.py"
    compare(sys.executable, False, shared)
    compare(binary, True, shared)
    print("CPython and CloverVM shutil fixtures passed")
