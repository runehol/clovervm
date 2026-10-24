# CloverVM-specific acceptance checks for the agreed reduced UserDict surface.
import collections
from collections import UserDict

assert collections.__all__ == ['UserDict']
assert not hasattr(UserDict, 'fromkeys')
assert not hasattr(collections, 'Counter')
assert not hasattr(collections, 'defaultdict')
assert not hasattr(collections, 'namedtuple')


class Subclass(UserDict):
    pass


try:
    Subclass(a=1).copy()
    assert False
except NotImplementedError:
    pass

# Ordinary views deliberately have no set-operation support yet.
try:
    UserDict(a=1).keys() | UserDict(b=2).keys()
    assert False
except TypeError:
    pass
