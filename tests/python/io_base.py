import io
import _io

assert io.IOBase is _io.IOBase
assert io.RawIOBase is _io.RawIOBase
assert issubclass(io.RawIOBase, io.IOBase)
assert issubclass(io.FileIO, io.RawIOBase)

for cls in (io.IOBase, io.RawIOBase):
    f = cls()
    assert isinstance(f, io.IOBase)
    assert not f.closed
    assert not f.readable() and not f.writable() and not f.seekable()
    assert f.flush() is None
    assert f.isatty() is False
    assert f.__enter__() is f
    for method in (cls.fileno, cls.tell, cls._checkReadable, cls._checkWritable, cls._checkSeekable):
        try:
            method(f)
            assert False
        except io.UnsupportedOperation:
            pass
    for method in (cls.seek, cls.truncate):
        try:
            method(f, 0)
            assert False
        except io.UnsupportedOperation:
            pass
    try:
        f.closed = True
        assert False
    except AttributeError:
        pass
    f.extra = 42
    assert f.extra == 42
    assert f.close() is None
    assert f.closed
    assert f.close() is None
    for method in (cls.flush, cls._checkClosed, cls.__enter__, cls.isatty):
        try:
            method(f)
            assert False
        except ValueError:
            pass
    for args in ((1,), ()):
        try:
            if args:
                cls(1)
            else:
                cls(unknown=True)
            assert False
        except TypeError:
            pass

r = io.RawIOBase()
for method in (io.RawIOBase.read, io.RawIOBase.readinto, io.RawIOBase.write):
    try:
        method(r, 1)
        assert False
    except NotImplementedError:
        pass
r.close()

class Stream(io.RawIOBase):
    def __init__(self, value, *, other=7):
        self.value = value
        self.other = other
    def readable(self):
        return self.value
    def writable(self):
        return True
    def seekable(self):
        return True
    def seek(self, offset, whence=0):
        return offset + whence + self.other

s = Stream(3, other=10)
assert not s.closed and s.value == 3 and s.other == 10
assert s._checkReadable() is None
assert s._checkWritable() is None
assert s._checkSeekable() is None
assert s.tell() == 11
s.close()

# Inherited initializers and ordinary-mixin order must preserve native storage.
class Child(Stream):
    pass
assert Child(5).value == 5
class Mixin:
    def marker(self):
        return 42
class Mixed(Mixin, io.RawIOBase):
    def __init__(self, value):
        self.value = value
m = Mixed(12)
assert m.marker() == 42 and not m.closed and m.value == 12
m.close()
class BaseChild(io.IOBase):
    pass
class Upgrade(BaseChild, io.RawIOBase):
    pass
u = Upgrade()
assert isinstance(u, io.RawIOBase) and not u.closed
u.extra = 19
assert u.extra == 19
u.close()
class Compatible(io.RawIOBase, io.IOBase):
    pass
c = Compatible()
assert not c.closed
c.close()
try:
    class Bad(io.IOBase, list):
        pass
    assert False
except TypeError:
    pass
try:
    io.IOBase.__new__(io.FileIO)
    assert False
except TypeError:
    pass

# Repeated calls exercise cached constructors; changing __init__ invalidates it.
def make():
    return Stream(3)
assert make().other == 7
assert make().other == 7
def replacement(self, value):
    self.other = 99
Stream.__init__ = replacement
assert make().other == 99
assert make().other == 99
class BadInit(io.IOBase):
    def __init__(self):
        return 1
try:
    BadInit()
    assert False
except TypeError:
    pass

class FlushError(Exception):
    pass
class Failing(io.IOBase):
    def __init__(self):
        self.calls = 0
    def flush(self):
        self.calls += 1
        raise FlushError()
f = Failing()
try:
    f.close()
    assert False
except FlushError:
    pass
assert f.closed and f.calls == 1
f.close()
assert f.calls == 1
class Normal(io.IOBase):
    def __init__(self):
        self.calls = 0
    def flush(self):
        self.calls += 1
        return 123
n = Normal()
assert n.close() is None
assert n.closed and n.calls == 1

# Base context exit calls the overridden close method.
class Context(io.IOBase):
    def close(self):
        self.called = True
ctx = Context()
try:
    with ctx as entered:
        assert entered is ctx
        raise FlushError()
    assert False
except FlushError:
    pass
assert ctx.called

# Receiver guards must reject unrelated objects before accessing a native prefix.
for method in (io.IOBase.close, io.IOBase.flush, io.IOBase.readable,
               io.IOBase._checkClosed, io.RawIOBase.read):
    try:
        if method is io.RawIOBase.read:
            method(object(), 1)
        else:
            method(object())
        assert False
    except TypeError:
        pass

class StopFlush(io.IOBase):
    def flush(self):
        raise StopIteration()
f = StopFlush()
try:
    f.close()
    assert False
except StopIteration:
    pass
assert f.closed
f.close()
