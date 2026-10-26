import io
import os

path = "clover-fileio-subclasses-" + str(os.getpid())

with io.FileIO(path, "wb") as fixture:
    fixture.write(b"abc")

class Derived(io.FileIO):
    def marker(self):
        return 42
    def flush(self):
        self.flushed = True
        return io.IOBase.flush(self)

with Derived(path, "rb") as f:
    assert f.__class__ is Derived
    assert isinstance(f, io.FileIO) and isinstance(f, io.RawIOBase)
    assert not f.closed and f.mode == "rb" and f.name == path
    f.extra = "subclass attribute"
    assert f.extra == "subclass attribute" and f.marker() == 42
    assert f.read(1) == b"a"
assert f.closed and f.flushed
assert f.extra == "subclass attribute"

class FurtherDerived(Derived):
    pass
with FurtherDerived(file=path, mode="rb") as f:
    assert f.__class__ is FurtherDerived
    assert f.marker() == 42
assert f.closed and f.flushed

class BaseSibling(io.IOBase):
    pass
class Mixed(BaseSibling, Derived):
    pass
with Mixed(path) as f:
    assert f.__class__ is Mixed
    assert f.name == path and f.mode == "rb" and not f.closed
    assert f.marker() == 42
    f.extra = 19
    assert f.read(1) == b"a" and f.extra == 19
assert f.closed and f.flushed

f = io.FileIO.__new__(Derived, path)
assert f.__class__ is Derived
f.close()
os.unlink(path)
try:
    class Incompatible(io.FileIO, list):
        pass
    assert False
except TypeError:
    pass

try:
    io.FileIO.__new__(object, path)
    assert False
except TypeError:
    pass
for cls in (io.IOBase, io.RawIOBase, 42):
    try:
        io.FileIO.__new__(cls, path)
        assert False
    except TypeError:
        pass
try:
    io.FileIO.read(object(), 1)
    assert False
except TypeError:
    pass

