import io
import _io
import os

assert io.FileIO is _io.FileIO
assert issubclass(io.UnsupportedOperation, OSError)
assert issubclass(io.UnsupportedOperation, ValueError)
# CTest may use any build directory; this fixture needs only its working directory.
path = "clover-fileio-" + str(os.getpid())

with io.FileIO(path, "wb") as f:
    assert not f.closed
    assert f.closefd
    assert f.name is path
    assert f.mode == "wb"
    assert f.writable() and not f.readable()
    assert f.seekable()
    assert f.fileno() >= 0
    assert f.write(b"abcdef") == 6
    assert f.tell() == 6
    try:
        f.read(1)
        assert False
    except io.UnsupportedOperation:
        pass
assert f.closed
f.close()

with io.FileIO(file=path, mode="rb", closefd=True) as f:
    assert f.readable() and not f.writable()
    assert f.read(2) == b"ab"
    assert f.read(0) == b""
    assert f.read(100) == b"cdef"
    assert f.read(1) == b""
    assert f.seek(1) == 1
    assert f.read(1) == b"b"
    assert f.seek(-1, 2) == 5
    assert f.read(2) == b"f"
    try:
        f.write(b"x")
        assert False
    except io.UnsupportedOperation:
        pass
    for size in (-1, None):
        try:
            f.read(size)
            assert False
        except NotImplementedError:
            pass
    try:
        f.read("2")
        assert False
    except TypeError:
        pass
    try:
        f.seek(0, 99)
        assert False
    except OSError:
        pass
    try:
        f.seek(2 ** 100)
        assert False
    except OverflowError:
        pass

for spelling in ("rbb", "brb", "bb+r", "rb+b"):
    with io.FileIO(path, spelling) as f:
        assert f.mode == ("rb+" if "+" in spelling else "rb")

with io.FileIO(path, "a+") as f:
    assert f.mode == "ab+"
    assert f.tell() == 6
    assert f.seek(0) == 0
    assert f.write(b"g") == 1
    assert f.seek(0) == 0
    assert f.read(20) == b"abcdefg"
    f.extra = 42
    assert f.extra == 42
    f.name = "label"
    assert f.name == "label"
    try:
        f.closed = True
        assert False
    except AttributeError:
        pass
    try:
        f.closefd = False
        assert False
    except AttributeError:
        pass
    try:
        f.mode = "wb"
        assert False
    except AttributeError:
        pass
    try:
        f.write("text")
        assert False
    except TypeError:
        pass

try:
    with io.FileIO(path) as f:
        raise RuntimeError("body")
except RuntimeError as error:
    assert str(error) == "body"
assert f.closed
for method in (io.FileIO.fileno, io.FileIO.tell, io.FileIO.readable, io.FileIO.writable, io.FileIO.seekable, io.FileIO.__enter__):
    try:
        method(f)
        assert False
    except ValueError:
        pass
try:
    f.read(0)
    assert False
except ValueError:
    pass
try:
    f.write(b"")
    assert False
except ValueError:
    pass

try:
    io.FileIO(path, "x")
    assert False
except FileExistsError as error:
    assert error.filename is path
os.unlink(path)
with io.FileIO(path, "x+") as f:
    assert f.mode == "xb+"
    assert f.write(b"new") == 3
with io.FileIO(path, "w+") as f:
    assert f.mode == "rb+"
    assert f.read(1) == b""
os.unlink(path)
try:
    io.FileIO(path)
    assert False
except FileNotFoundError as error:
    assert error.filename is path
for mode in ("", "rr", "r++", "rw", "rt", "q"):
    try:
        io.FileIO(path, mode)
        assert False
    except ValueError:
        pass
try:
    io.FileIO(path, closefd=False)
    assert False
except ValueError:
    pass
try:
    io.FileIO("bad\x00path", "w")
    assert False
except ValueError:
    pass
try:
    io.FileIO(-1)
    assert False
except ValueError:
    pass
try:
    io.FileIO(2 ** 100)
    assert False
except OverflowError:
    pass
try:
    io.FileIO(".")
    assert False
except IsADirectoryError:
    pass
try:
    class Derived(io.FileIO):
        pass
    assert False
except TypeError:
    pass

try:
    io.FileIO.__new__(object, path)
    assert False
except TypeError:
    pass
try:
    io.FileIO.read(object(), 1)
    assert False
except TypeError:
    pass

class OwnershipFlag:
    def __bool__(self):
        raise RuntimeError("ownership conversion")

try:
    io.FileIO(path, "w", closefd=OwnershipFlag())
    assert False
except RuntimeError as error:
    assert str(error) == "ownership conversion"
assert not os.path.exists(path)

try:
    io.FileIO(None, "invalid")
    assert False
except TypeError:
    pass
try:
    io.FileIO(path, "invalid", closefd=OwnershipFlag())
    assert False
except RuntimeError:
    pass

class ReentrantOwnershipFlag:
    def __bool__(self):
        with io.FileIO(path, "wb") as nested:
            assert nested.write(b"nested") == 6
        return True

with io.FileIO(path, closefd=ReentrantOwnershipFlag()) as f:
    assert f.read(6) == b"nested"
os.unlink(path)

unicode_path = path + "-\u00e9"
with io.FileIO(unicode_path, "wb") as f:
    assert f.name is unicode_path
    assert f.write(b"unicode path") == 12
with io.FileIO(unicode_path) as f:
    assert f.read(100) == b"unicode path"
os.unlink(unicode_path)
