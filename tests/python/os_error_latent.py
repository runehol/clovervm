# CloverVM's agreed ordinary-attribute semantics for characters_written.
import errno

def read_count(e):
    return e.characters_written

def write_count(e, value):
    e.characters_written = value

for cls in (OSError, BlockingIOError, FileNotFoundError):
    e = cls()
    for i in range(100):
        assert not hasattr(e, 'characters_written')
        write_count(e, i)
        assert read_count(e) == i
        del e.characters_written
        assert not hasattr(e, 'characters_written')
        try:
            read_count(e)
            assert False
        except AttributeError:
            pass
    e.characters_written = 'ordinary attribute'
    assert e.characters_written == 'ordinary attribute'
    e.characters_written = None
    assert e.characters_written is None
    e.characters_written = -1
    assert e.characters_written == -1
    del e.characters_written
    try:
        del e.characters_written
        assert False
    except AttributeError:
        pass

class Error(OSError):
    characters_written = 'class fallback'

x = Error()
assert x.characters_written == 'class fallback'
x.characters_written = ['owned', 123]
assert x.characters_written == ['owned', 123]
del x.characters_written
assert x.characters_written == 'class fallback'
x.extra = 'payload'
assert x.extra == 'payload'

class Custom(OSError):
    def __init__(self, n):
        self.characters_written = ['count', n]

c = Custom(5)
assert c.characters_written == ['count', 5]
assert c.args == ()

class E(OSError):
    pass

# Constructor thunk invalidation when an inherited initializer changes.
for i in range(30):
    assert E(errno.ENOENT, 'missing').errno == errno.ENOENT

def custom(self, n):
    self.characters_written = n

E.__init__ = custom
for i in range(30):
    x = E(i)
    assert x.args == () and x.errno is None
    assert x.characters_written == i

del E.__init__
assert E(errno.EEXIST, 'exists').errno == errno.EEXIST

# All OS fields use reserved latent slots, with class-level None fallbacks.
assert OSError.errno is None
assert OSError.strerror is None
assert OSError.filename is None
assert OSError.filename2 is None

class Defaults(OSError):
    errno = 'class errno'
    strerror = 'class message'
    filename = 'class path'
    filename2 = 'class destination'

x = Defaults()
assert x.errno == 'class errno' and x.strerror == 'class message'
assert x.filename == 'class path' and x.filename2 == 'class destination'
# Formatting uses instance fields, not the inherited defaults.
assert str(x) == ''
for i in range(100):
    x.errno = i
    x.strerror = 'message'
    x.filename = None
    x.filename2 = 'destination'
    assert x.errno == i and x.filename is None
    assert str(x) == '[Errno ' + str(i) + "] message: None -> 'destination'"
    del x.errno
    del x.strerror
    del x.filename
    del x.filename2
    assert x.errno == 'class errno' and x.filename == 'class path'
    assert str(x) == ''

# Initialization activates native slots even when subclasses provide defaults.
x = Defaults(errno.ENOENT, 'missing', 'path')
assert x.errno == errno.ENOENT and x.strerror == 'missing' and x.filename == 'path'
assert x.filename2 == 'class destination'
assert str(x) == '[Errno ' + str(errno.ENOENT) + "] missing: 'path'"
