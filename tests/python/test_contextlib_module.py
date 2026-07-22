# Python test set -- contextlib module
#
# Surface selected from Python 3.14's contextlib module help. This file does
# not copy CPython test code.

import contextlib


class Resource:
    def __init__(self):
        self.closed = False

    def close(self):
        self.closed = True


manager = contextlib.AbstractContextManager()
assert manager.__enter__() is manager
assert manager.__exit__(None, None, None) is None

with contextlib.nullcontext(42) as value:
    assert value == 42

with contextlib.nullcontext() as value:
    assert value is None

resource = Resource()
with contextlib.closing(resource) as value:
    assert value is resource
    assert not resource.closed
assert resource.closed

with contextlib.suppress(ValueError):
    raise ValueError

after_suppressed = 7

try:
    with contextlib.suppress(TypeError):
        raise ValueError
    assert False
except ValueError:
    after_suppressed = 11

assert after_suppressed == 11

with contextlib.suppress(ValueError, TypeError):
    raise TypeError

assert contextlib.__all__[0] == "AbstractContextManager"
assert contextlib.__all__[3] == "suppress"
