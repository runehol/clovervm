# Original regressions checked against CPython 3.14.5.
import errno

assert IOError is OSError
assert EnvironmentError is OSError
assert issubclass(OSError, Exception)
assert issubclass(BrokenPipeError, ConnectionError)
classes = (BlockingIOError, ChildProcessError, ConnectionError, BrokenPipeError,
           ConnectionAbortedError, ConnectionRefusedError, ConnectionResetError,
           FileExistsError, FileNotFoundError, InterruptedError, IsADirectoryError,
           NotADirectoryError, PermissionError, ProcessLookupError, TimeoutError)
for cls in classes:
    assert issubclass(cls, OSError)
    e = cls()
    assert e.args == ()
    assert e.errno is None
    assert e.strerror is None
    assert e.filename is None
    assert e.filename2 is None
    assert not hasattr(e, 'characters_written')
    assert str(e) == ''
    e = cls(errno.ENOENT, 'missing')
    assert isinstance(e, cls)
    assert e.args == (errno.ENOENT, 'missing')
    assert str(e) == '[Errno ' + str(errno.ENOENT) + '] missing'

mappings = ((errno.ENOENT, FileNotFoundError), (errno.EEXIST, FileExistsError),
            (errno.EACCES, PermissionError), (errno.EPERM, PermissionError),
            (errno.ENOTDIR, NotADirectoryError), (errno.EISDIR, IsADirectoryError),
            (errno.EPIPE, BrokenPipeError), (errno.EAGAIN, BlockingIOError),
            (errno.EALREADY, BlockingIOError), (errno.EINPROGRESS, BlockingIOError),
            (errno.EINTR, InterruptedError), (errno.ECHILD, ChildProcessError),
            (errno.ECONNREFUSED, ConnectionRefusedError),
            (errno.ECONNABORTED, ConnectionAbortedError),
            (errno.ECONNRESET, ConnectionResetError),
            (errno.ESRCH, ProcessLookupError), (errno.ETIMEDOUT, TimeoutError))
for pair in mappings:
    number = pair[0]
    cls = pair[1]
    e = OSError(number, 'failure', 'old', None, 'new')
    assert isinstance(e, cls)
    assert e.errno == number
    assert e.strerror == 'failure'
    assert e.filename == 'old'
    assert e.filename2 == 'new'
    assert e.args == (number, 'failure')
    assert str(e) == '[Errno ' + str(number) + "] failure: 'old' -> 'new'"
    assert repr(e) == cls.__name__ + '(' + str(number) + ", 'failure')"

for args in ((), ('message',), (1, 2, 3, 4, 5, 6)):
    e = OSError(*args)
    assert e.args == args
    assert e.errno is None
    assert e.strerror is None

for number in (errno.ENOSPC, errno.EIO, errno.EBADF, 10**40, -(10**40)):
    e = OSError(number, 'failure')
    assert not isinstance(e, FileNotFoundError)
    assert e.errno == number

assert str(OSError(None, None)) == '[Errno None] None'
e = OSError(errno.ENOENT, 'missing', None, 99, 'unused')
assert e.args == (errno.ENOENT, 'missing', None, 99, 'unused')
assert e.filename is None and e.filename2 is None

e = BlockingIOError(errno.EAGAIN, 'blocked', 7)
assert e.characters_written == 7
assert e.args == (errno.EAGAIN, 'blocked', 7)
assert e.filename is None
assert str(e) == '[Errno ' + str(errno.EAGAIN) + '] blocked'
assert not hasattr(BlockingIOError(errno.EAGAIN, 'blocked', -1), 'characters_written')

class Custom(OSError):
    def __init__(self, message, *, detail=0):
        assert self.args == ()
        assert self.errno is None
        assert not hasattr(self, 'characters_written')
        self.message = message
        self.detail = detail

c = Custom('hello', detail=3)
assert c.args == () and c.detail == 3 and c.message == 'hello'
OSError.__init__(c, errno.ENOENT, 'missing', 'path')
assert c.errno == errno.ENOENT and c.filename == 'path'
assert c.args == (errno.ENOENT, 'missing')
OSError.__init__(c, 5, 'other')
assert c.errno == 5 and c.filename == 'path'

class Inherited(OSError):
    pass

x = Inherited(errno.ENOENT, 'missing')
assert isinstance(x, Inherited) and not isinstance(x, FileNotFoundError)
blank = OSError.__new__(Custom, 'hello', detail=5)
assert blank.args == () and blank.errno is None
x = OSError.__new__(OSError, errno.ENOENT, 'missing', 'path')
assert isinstance(x, FileNotFoundError) and x.filename == 'path'
OSError.__init__(x, errno.EEXIST, 'different')
assert x.errno == errno.ENOENT

for i in range(80):
    try:
        raise OSError(errno.ENOENT, 'missing', 'path')
    except FileNotFoundError as e:
        assert e.filename == 'path'
    try:
        OSError(code=errno.ENOENT)
        assert False
    except TypeError:
        pass
    try:
        OSError.__new__(Exception, 1, 'bad')
        assert False
    except TypeError:
        pass

class A(Exception):
    pass
class B(A, Inherited):
    pass
class C(Inherited, A):
    pass
class D(FileNotFoundError, PermissionError):
    pass
class E(OSError, Exception):
    pass
for cls in (B, C, D, E):
    e = cls(errno.ENOENT, 'missing', 'path')
    assert isinstance(e, cls) and e.filename == 'path'
    e.extra = 42
    assert e.extra == 42

try:
    class Conflict(OSError, StopIteration):
        pass
    assert False
except TypeError:
    pass
try:
    class Conflict2(StopIteration, OSError):
        pass
    assert False
except TypeError:
    pass

