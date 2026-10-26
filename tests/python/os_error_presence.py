# Original presence/formatting regressions checked against CPython 3.14.5.
# Separate file to stay within the per-code-object u8 cache-index budget.
# Unset fields and explicitly supplied None have distinct native presence.
class BaseInit(OSError):
    __init__ = BaseException.__init__

for args in ((None, None), ('a', 'b'), (1, 2, 3)):
    e = BaseInit(*args)
    assert e.errno is None and e.strerror is None
    assert str(e) == str(args)
    assert e.args == args

assert str(OSError()) == ''
assert str(OSError(None, None)) == '[Errno None] None'
e = OSError()
e.errno = None
e.strerror = None
assert str(e) == '[Errno None] None'
e.filename = None
assert str(e) == '[Errno None] None: None'
e.filename2 = None
assert str(e) == '[Errno None] None: None -> None'
del e.filename
del e.filename2
del e.errno
del e.strerror
assert e.errno is None and e.filename is None
assert str(e) == ''

class Deferred(OSError):
    def __init__(self):
        pass
c = Deferred()
OSError.__init__(c, 5, 'old', 'path')
OSError.__init__(c, 'message')
assert c.args == ('message',)
assert c.errno is None and c.strerror is None
assert c.filename == 'path'
assert str(c) == "[Errno None] None: 'path'"

# The family-specific thunk preserves initializer errors and return checking.
# Use a module-global failure because closures are outside this test's scope.
initializer_failure = ValueError('initializer failed')
class RaisingInit(OSError):
    def __init__(self):
        raise initializer_failure
try:
    RaisingInit()
    assert False
except ValueError as caught:
    assert caught is initializer_failure

class BadReturn(OSError):
    def __init__(self):
        return 1
try:
    BadReturn()
    assert False
except TypeError:
    pass
