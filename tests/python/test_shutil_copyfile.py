import io
import os
import shutil
import stat


def raises(expected, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except expected as error:
        return error
    assert False


def write(path, data):
    with io.FileIO(path, "wb") as stream:
        assert stream.write(data) == len(data)


def read(path):
    with io.FileIO(path, "rb") as stream:
        return stream.readall()


fixture = "clover-shutil-paths-" + str(os.getpid())
src = fixture + "/src"
dst = fixture + "/dst"
directory = fixture + "/directory"
joined = directory + "/src"
missing = fixture + "/missing"
partial = fixture + "/partial"
mode_failed = fixture + "/mode-failed"
private_mode = 448  # 0o700
source_mode = 489  # 0o751
old_mode = 416  # 0o640
os.mkdir(fixture, private_mode)
try:
    os.mkdir(directory, private_mode)
    data = b"\x00test\xff"
    for _ in range(16):
        data = data + data
    write(src, data)
    os.chmod(src, source_mode)

    write(dst, b"old")
    os.chmod(dst, old_mode)
    assert shutil.copyfile(dst=dst, src=src) is dst
    assert read(dst) == data
    assert stat.S_IMODE(os.stat(dst)[stat.ST_MODE]) == old_mode

    write(src, b"short")
    assert shutil.copyfile(src, dst, follow_symlinks=False) is dst
    assert read(dst) == b"short"
    assert shutil.copy(src, directory) == joined
    assert read(joined) == b"short"
    assert stat.S_IMODE(os.stat(joined)[stat.ST_MODE]) == source_mode
    assert shutil.copy(src, dst) is dst
    assert stat.S_IMODE(os.stat(dst)[stat.ST_MODE]) == source_mode

    assert isinstance(raises(shutil.SameFileError, shutil.copyfile, src, src),
                      shutil.Error)
    raises(shutil.SameFileError, shutil.copy, src, fixture)
    raises(IsADirectoryError, shutil.copyfile, src, directory)
    raises(IsADirectoryError, shutil.copyfile, directory, missing)
    raises(FileNotFoundError, shutil.copyfile, src, missing + "/")
    assert raises(FileNotFoundError, shutil.copyfile, missing, directory).filename == missing
    raises(ValueError, shutil.copyfile, src + "\x00tail", src)
    raises(ValueError, shutil.copy, src, directory + "\x00tail")
    for copy in (shutil.copyfile, shutil.copy):
        raises(TypeError, copy, src)
        raises(TypeError, copy, src, dst, False)
        raises(TypeError, copy, src, dst, follow_links=False)
        raises(TypeError, copy, None, dst)
    assert read(src) == b"short"
    assert read(dst) == b"short"

    # Force the buffered fallback on CPython so the injected callback runs.
    if hasattr(shutil, "_HAS_FCOPYFILE"):
        shutil._HAS_FCOPYFILE = False
        shutil._USE_CP_SENDFILE = False
        shutil._USE_CP_COPY_FILE_RANGE = False
    streams = []
    failure = RuntimeError("copy transfer failed")

    def fail_after_write(source, destination):
        streams.extend((source, destination))
        destination.write(source.read(2))
        raise failure

    old_copyfileobj = shutil.copyfileobj
    shutil.copyfileobj = fail_after_write
    try:
        write(partial, b"old contents")
        os.chmod(partial, old_mode)
        assert raises(RuntimeError, shutil.copy, src, partial) is failure
    finally:
        shutil.copyfileobj = old_copyfileobj
    assert streams[0].closed and streams[1].closed
    assert read(partial) == b"sh"
    assert stat.S_IMODE(os.stat(partial)[stat.ST_MODE]) == old_mode

    failure = PermissionError("chmod failed")

    def fail_chmod(path, mode):
        assert path == mode_failed and mode == source_mode
        assert read(path) == b"short"
        raise failure

    old_chmod = os.chmod
    os.chmod = fail_chmod
    try:
        assert raises(PermissionError, shutil.copy, src, mode_failed) is failure
    finally:
        os.chmod = old_chmod
    assert read(mode_failed) == b"short"
finally:
    for path in (src, dst, joined, partial, mode_failed):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
    if os.path.isdir(directory):
        os.rmdir(directory)
    os.rmdir(fixture)
