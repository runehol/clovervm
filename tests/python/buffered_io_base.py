import io

assert issubclass(io.BufferedIOBase, io.IOBase)
assert not issubclass(io.BufferedIOBase, io.RawIOBase)
f = io.BufferedIOBase()
assert not f.closed
assert not f.readable() and not f.writable() and not f.seekable()
assert f.flush() is None
assert f.__enter__() is f
for method in (io.BufferedIOBase.read, io.BufferedIOBase.read1, io.BufferedIOBase.detach):
    try:
        method(f)
        assert False
    except io.UnsupportedOperation:
        pass
try:
    f.write(None)
    assert False
except io.UnsupportedOperation:
    pass
for method in (io.BufferedIOBase.readinto, io.BufferedIOBase.readinto1):
    try:
        method(f, bytearray())
        assert False
    except io.UnsupportedOperation:
        pass
for method in (io.BufferedIOBase.read, io.BufferedIOBase.read1):
    for value in (None, '2', 1.5):
        try:
            method(f, value)
            assert False
        except TypeError:
            pass
    try:
        method(f, 2**100)
        assert False
    except OverflowError:
        pass
    try:
        method(f, size=1)
        assert False
    except TypeError:
        pass
try:
    f.closed = True
    assert False
except AttributeError:
    pass
f.close()
assert f.closed
f.close()
try:
    f.flush()
    assert False
except ValueError:
    pass
class Stream(io.BufferedIOBase):
    def __init__(self, value):
        self.value = value
    def flush(self):
        self.flushed = self.value
s = Stream(42)
assert s.value == 42 and not s.closed
s.close()
assert s.closed and s.flushed == 42
class Child(Stream):
    pass
assert Child(7).value == 7
# Constructor caches must invalidate when the initializer changes.
def make():
    return Stream(3)
assert make().value == 3
def replacement(self, value):
    self.value = value + 10
Stream.__init__ = replacement
assert make().value == 13
assert make().value == 13
class Failure(Exception):
    pass
error = Failure('flush')
class Failing(io.BufferedIOBase):
    def flush(self):
        raise error
s = Failing()
try:
    s.close()
    assert False
except Failure as caught:
    assert caught is error
assert s.closed
s.close()
for method in (io.BufferedIOBase.read, io.BufferedIOBase.read1,
               io.BufferedIOBase.write, io.BufferedIOBase.readinto,
               io.BufferedIOBase.readinto1):
    try:
        method(io.RawIOBase(), 0)
        assert False
    except TypeError:
        pass
try:
    io.BufferedIOBase.detach(object())
    assert False
except TypeError:
    pass
try:
    io.BufferedIOBase(1)
    assert False
except TypeError:
    pass
try:
    class Bad(io.BufferedIOBase, list):
        pass
    assert False
except TypeError:
    pass

class Index:
    def __index__(self):
        self.calls += 1
        return self.value
for method in (io.BufferedIOBase.read, io.BufferedIOBase.read1):
    index = Index()
    index.calls = 0
    index.value = 1
    try:
        method(io.BufferedIOBase(), index)
        assert False
    except io.UnsupportedOperation:
        pass
    assert index.calls == 1
    index.value = 'bad'
    try:
        method(io.BufferedIOBase(), index)
        assert False
    except TypeError:
        pass
    assert index.calls == 2
    # Invalid self is rejected before argument conversion.
    try:
        method(object(), index)
        assert False
    except TypeError:
        pass
    assert index.calls == 2


# The abstract stubs parse C int sizes, even though raw read accepts a wider
# index-sized size. Check both endpoints and the adjacent out-of-range values.
class IntTrap(int):
    def __index__(self):
        raise AssertionError('use stored int payload')
    def __lt__(self, other):
        raise AssertionError('use stored int payload')
    def __gt__(self, other):
        raise AssertionError('use stored int payload')
f = io.BufferedIOBase()
for method in (io.BufferedIOBase.read, io.BufferedIOBase.read1):
    for value in (-(2**31), 2**31 - 1):
        for size in (value, IntTrap(value)):
            try:
                method(f, size)
                assert False
            except io.UnsupportedOperation:
                pass
        index = Index()
        index.value = value
        index.calls = 0
        try:
            method(f, index)
            assert False
        except io.UnsupportedOperation:
            pass
        assert index.calls == 1
    for value in (-(2**31) - 1, 2**31, -(2**100), 2**100):
        for size in (value, IntTrap(value)):
            try:
                method(f, size)
                assert False
            except OverflowError:
                pass
        index = Index()
        index.value = value
        index.calls = 0
        try:
            method(f, index)
            assert False
        except OverflowError:
            pass
        assert index.calls == 1
f.close()
