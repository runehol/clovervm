"""Functional forms of Python operators.

This is a CloverVM-focused subset of CPython's :mod:`operator` module.  It
implements operations that the VM can express directly today and leaves
protocol-heavy helpers explicit rather than pretending unsupported dunder
dispatch exists.
"""

from builtins import abs as _builtin_abs


def abs(a):
    return _builtin_abs(a)


def add(a, b):
    return a + b


def and_(a, b):
    return a & b


def call(obj, /, *args, **kwargs):
    return obj(*args, **kwargs)


def concat(a, b):
    return a + b


def contains(a, b):
    return b in a


def countOf(a, b):
    count = 0
    for item in a:
        if item is b or item == b:
            count += 1
    return count


def delitem(a, b):
    del a[b]


def eq(a, b):
    return a == b


def floordiv(a, b):
    return a // b


def ge(a, b):
    return a >= b


def getitem(a, b):
    return a[b]


def gt(a, b):
    return a > b


def iadd(a, b):
    a += b
    return a


def iand(a, b):
    a &= b
    return a


def iconcat(a, b):
    return a + b


def ifloordiv(a, b):
    a //= b
    return a


def ilshift(a, b):
    a <<= b
    return a


def imatmul(a, b):
    a @= b
    return a


def imod(a, b):
    a %= b
    return a


def imul(a, b):
    a *= b
    return a


def index(a):
    if isinstance(a, int):
        return int(a)
    raise TypeError


def indexOf(a, b):
    idx = 0
    for item in a:
        if item is b or item == b:
            return idx
        idx += 1
    raise ValueError


def inv(a):
    return ~a


def invert(a):
    return ~a


def ior(a, b):
    a |= b
    return a


def ipow(a, b):
    a **= b
    return a


def irshift(a, b):
    a >>= b
    return a


def is_(a, b):
    return a is b


def is_none(a):
    return a is None


def is_not(a, b):
    return a is not b


def is_not_none(a):
    return a is not None


def isub(a, b):
    a -= b
    return a


def itruediv(a, b):
    a /= b
    return a


def ixor(a, b):
    a ^= b
    return a


def le(a, b):
    return a <= b


# Signed platform-size limits for the VM's current 64-bit targets.
# sys.maxsize currently exposes the narrower SMI limit.
_SSIZE_MIN = -(2**63)
_SSIZE_MAX = 2**63 - 1


def _check_ssize(value):
    if value < _SSIZE_MIN or value > _SSIZE_MAX:
        raise OverflowError("Python int too large to convert to C ssize_t")
    return value


def length_hint(obj, default=0, /):
    default = _check_ssize(index(default))
    try:
        length = index(len(obj))
        if length < 0:
            raise ValueError("__len__() should return >= 0")
        length = _check_ssize(length)
    except TypeError:
        pass
    else:
        return length

    try:
        hint = __clover_call_special__(
            obj, "__length_hint__", TypeError, "object has no __length_hint__"
        )
    except TypeError:
        return default
    if hint is NotImplemented:
        return default
    if not isinstance(hint, int):
        raise TypeError("__length_hint__ must be an integer")
    hint = _check_ssize(int(hint))
    if hint < 0:
        raise ValueError("__length_hint__() should return >= 0")
    return hint


def lshift(a, b):
    return a << b


def lt(a, b):
    return a < b


def matmul(a, b):
    return a @ b


def mod(a, b):
    return a % b


def mul(a, b):
    return a * b


def ne(a, b):
    return a != b


def neg(a):
    return -a


def not_(a):
    return not a


def or_(a, b):
    return a | b


def pos(a):
    return +a


def pow(a, b):
    return a**b


def rshift(a, b):
    return a >> b


def setitem(a, b, c):
    a[b] = c


def sub(a, b):
    return a - b


def truediv(a, b):
    return a / b


def truth(a):
    if a:
        return True
    return False


def xor(a, b):
    return a ^ b


def _resolve_attr_path(obj, path):
    for name in path:
        obj = getattr(obj, name)
    return obj


class attrgetter:
    """Return a callable that fetches one or more attributes."""

    def __init__(self, attr, /, *attrs):
        paths = []
        for name in (attr,) + attrs:
            if not isinstance(name, str):
                raise TypeError("attribute name must be a string")
            parts = str.split(name, ".")
            paths.append((name,) if len(parts) == 1 else tuple(parts))
        self._paths = tuple(paths)

    def __call__(self, obj, /):
        paths = self._paths
        if len(paths) == 1:
            return _resolve_attr_path(obj, paths[0])
        results = []
        for path in paths:
            results.append(_resolve_attr_path(obj, path))
        return tuple(results)


class itemgetter:
    """Return a callable that fetches one or more items."""

    def __init__(self, item, /, *items):
        self._items = (item,) + items

    def __call__(self, obj, /):
        items = self._items
        if len(items) == 1:
            return obj[items[0]]
        results = []
        for item in items:
            results.append(obj[item])
        return tuple(results)


class methodcaller:
    """Return a callable that invokes a named method with stored arguments."""

    def __init__(self, name, /, *args, **kwargs):
        if not isinstance(name, str):
            raise TypeError("method name must be a string")
        self._name = name
        self._args = args
        self._kwargs = kwargs

    def __call__(self, obj, /):
        method = getattr(obj, self._name)
        return method(*self._args, **self._kwargs)


__abs__ = abs
__add__ = add
__and__ = and_
__call__ = call
__concat__ = concat
__contains__ = contains
__delitem__ = delitem
__eq__ = eq
__floordiv__ = floordiv
__ge__ = ge
__getitem__ = getitem
__gt__ = gt
__iadd__ = iadd
__iand__ = iand
__iconcat__ = iconcat
__ifloordiv__ = ifloordiv
__ilshift__ = ilshift
__imatmul__ = imatmul
__imod__ = imod
__imul__ = imul
__index__ = index
__inv__ = inv
__invert__ = invert
__ior__ = ior
__ipow__ = ipow
__irshift__ = irshift
__isub__ = isub
__itruediv__ = itruediv
__ixor__ = ixor
__le__ = le
__lshift__ = lshift
__lt__ = lt
__matmul__ = matmul
__mod__ = mod
__mul__ = mul
__ne__ = ne
__neg__ = neg
__not__ = not_
__or__ = or_
__pos__ = pos
__pow__ = pow
__rshift__ = rshift
__setitem__ = setitem
__sub__ = sub
__truediv__ = truediv
__xor__ = xor


__all__ = (
    "abs",
    "add",
    "and_",
    "attrgetter",
    "call",
    "concat",
    "contains",
    "countOf",
    "delitem",
    "eq",
    "floordiv",
    "ge",
    "getitem",
    "gt",
    "iadd",
    "iand",
    "iconcat",
    "ifloordiv",
    "ilshift",
    "imatmul",
    "imod",
    "imul",
    "index",
    "indexOf",
    "inv",
    "invert",
    "ior",
    "ipow",
    "irshift",
    "is_",
    "is_none",
    "is_not",
    "is_not_none",
    "isub",
    "itemgetter",
    "itruediv",
    "ixor",
    "le",
    "length_hint",
    "lshift",
    "lt",
    "matmul",
    "methodcaller",
    "mod",
    "mul",
    "ne",
    "neg",
    "not_",
    "or_",
    "pos",
    "pow",
    "rshift",
    "setitem",
    "sub",
    "truediv",
    "truth",
    "xor",
)
