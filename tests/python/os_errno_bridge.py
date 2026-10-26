import os
import errno
import glob
from _test_native import (
    os_error, os_error_invalid_message, os_error_null_message,
    os_error_propagated,
)

# The bridge shares constructor field/args normalization and canonical
# errno-to-builtin-class selection.
for row in (
    (errno.ENOENT, FileNotFoundError),
    (errno.EACCES, PermissionError),
    (errno.EEXIST, FileExistsError),
    (errno.ENOTDIR, NotADirectoryError),
    (errno.EINVAL, OSError),
    (1234567, OSError),
    (errno.EAGAIN, BlockingIOError),
):
    number = row[0]
    cls = row[1]
    for pair in ((None, None), ("source", None), ("source", "destination")):
        first = pair[0]
        second = pair[1]
        try:
            os_error(number, first, second)
            assert False
        except OSError as error:
            assert error.__class__ is cls
            assert error.errno == number
            assert error.strerror == "native test error"
            assert error.args == (number, "native test error")
            assert error.filename is first
            assert error.filename2 is second
            assert not hasattr(error, "characters_written")

for row in (
    (os_error_invalid_message, "native extension error message must be valid UTF-8"),
    (os_error_null_message, "native extension error message must not be null"),
    (os_error_propagated, "original native error"),
):
    fn = row[0]
    message = row[1]
    try:
        fn()
        assert False
    except ValueError as error:
        assert str(error) == message

missing = "build-debug/clover-errno-missing-" + str(os.getpid())
other = missing + "-destination"
for operation in (os.stat, os.lstat, os.listdir, os.chdir, os.rmdir, os.unlink):
    try:
        operation(missing)
        assert False
    except FileNotFoundError as error:
        assert error.errno == 2
        assert error.filename is missing
        assert error.filename2 is None
        assert error.strerror == os.strerror(2)
        assert error.args == (2, error.strerror)
        assert str(error) == "[Errno 2] " + error.strerror + ": " + repr(missing)

try:
    os.chmod(missing, 511)
    assert False
except FileNotFoundError as error:
    assert error.filename is missing

try:
    os.rename(missing, other)
    assert False
except FileNotFoundError as error:
    assert error.filename is missing
    assert error.filename2 is other
    assert error.args == (2, error.strerror)
    assert str(error) == "[Errno 2] " + error.strerror + ": " + repr(missing) + " -> " + repr(other)

# A non-directory ancestor and an existing destination exercise mappings
# other than ENOENT without depending on permissions or the current user.
not_directory = "CMakeLists.txt/child"
try:
    os.stat(not_directory)
    assert False
except NotADirectoryError as error:
    assert error.errno == 20
    assert error.filename is not_directory

try:
    os.mkdir(".")
    assert False
except FileExistsError as error:
    assert error.errno == 17
    assert error.filename == "."

os.makedirs(".", exist_ok=True)
assert glob.glob(missing + "/*") == []
assert glob.glob("CMakeLists.txt/*") == []
assert not os.access(missing, os.F_OK)
for path in (missing, not_directory):
    assert not os._isdir(path)
    assert not os.path.exists(path)
    assert not os.path.lexists(path)
    assert not os.path.isdir(path)
    assert not os.path.isfile(path)
    assert not os.path.islink(path)
    assert not os.path.ismount(path)
try:
    os.path.samefile(missing, ".")
    assert False
except FileNotFoundError:
    pass

class Path(str):
    def __str__(self):
        assert False

original = Path(missing)
try:
    os.stat(original)
    assert False
except FileNotFoundError as error:
    assert error.filename is original

# The makedirs recovery catch must suppress only genuine existing directories.
os.makedirs(missing)
try:
    os.makedirs(missing, exist_ok=True)
    try:
        os.makedirs(missing)
        assert False
    except FileExistsError as error:
        assert error.filename == missing
finally:
    os.rmdir(missing)
