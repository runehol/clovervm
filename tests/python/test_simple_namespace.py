# Python test set -- types.SimpleNamespace representation
#
# Portions adapted from CPython's Lib/test/test_types.py at Python 3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

from types import SimpleNamespace as Namespace


ns = Namespace()
assert repr(ns) == "namespace()"
ns.b = 1
ns.a = "spam"
assert repr(ns) == "namespace(b=1, a='spam')"
ns.b = 2
assert repr(ns) == "namespace(b=2, a='spam')"
del ns.b
ns.b = 3
assert repr(ns) == "namespace(a='spam', b=3)"
ns.__dict__[""] = 4
ns.__dict__["x-y"] = 5
assert repr(ns) == "namespace(a='spam', b=3, x-y=5)"


class Derived(Namespace):
    pass


ns = Derived()
ns.x = 1
assert repr(ns) == "Derived(x=1)"
ns.self = ns
assert repr(ns) == "Derived(x=1, self=Derived(...))"

ns = Namespace()
ns.c = "cookie"
ns.spam = ns
assert repr(ns) == "namespace(c='cookie', spam=namespace(...))"
a = Namespace()
b = Namespace()
a.spam = b
b.x = 1
b.spam = a
assert repr(a) == "namespace(spam=namespace(x=1, spam=namespace(...)))"
ns = Namespace()
ns.values = [ns]
assert repr(ns) == "namespace(values=[namespace(...)])"
child = Namespace()
child.x = 1
ns = Namespace()
ns.a = child
ns.b = child
assert repr(ns) == "namespace(a=namespace(x=1), b=namespace(x=1))"


class RaisingRepr:
    def __repr__(self):
        raise ValueError("repr failed")


ns = Namespace()
ns.x = RaisingRepr()
try:
    repr(ns)
    assert False
except ValueError:
    pass
ns.x = 1
assert repr(ns) == "namespace(x=1)"


class DeletingRepr:
    def __repr__(self):
        del target.y
        target.z = 3
        return "deleted"


target = Namespace()
target.x = DeletingRepr()
target.y = 2
assert repr(target) == "namespace(x=deleted)"
