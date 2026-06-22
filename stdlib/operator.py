"""Functional forms of Python operators.

This is a CloverVM-focused subset of CPython's :mod:`operator` module.  It
implements operations that the VM can express directly today and leaves
protocol-heavy helpers explicit rather than pretending unsupported dunder
dispatch exists.
"""


def _unsupported():
    raise UnimplementedError


def abs(a):
    if a < 0:
        return -a
    return a


def add(a, b):
    return a + b


def and_(a, b):
    return a & b


def call(obj, *args):
    if len(args) == 0:
        return obj()
    if len(args) == 1:
        return obj(args[0])
    if len(args) == 2:
        return obj(args[0], args[1])
    if len(args) == 3:
        return obj(args[0], args[1], args[2])
    raise TypeError


def concat(a, b):
    return a.__add__(b)


def contains(a, b):
    return b in a


def countOf(a, b):
    count = 0
    for item in a:
        if item == b:
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
    return a + b


def iand(a, b):
    return a & b


def iconcat(a, b):
    return a.__add__(b)


def ifloordiv(a, b):
    return a // b


def ilshift(a, b):
    return a << b


def imatmul(a, b):
    return a @ b


def imod(a, b):
    return a % b


def imul(a, b):
    return a * b


def index(a):
    if isinstance(a, int):
        return a
    raise TypeError


def indexOf(a, b):
    idx = 0
    for item in a:
        if item == b:
            return idx
        idx += 1
    raise ValueError


def inv(a):
    return ~a


def invert(a):
    return ~a


def ior(a, b):
    return a | b


def ipow(a, b):
    return a**b


def irshift(a, b):
    return a >> b


def is_(a, b):
    return a is b


def is_none(a):
    return a is None


def is_not(a, b):
    return a is not b


def is_not_none(a):
    return a is not None


def isub(a, b):
    return a - b


def itruediv(a, b):
    return a / b


def ixor(a, b):
    return a ^ b


def le(a, b):
    return a <= b


def length_hint(obj, default=0):
    try:
        return len(obj)
    except TypeError:
        return default


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


def attrgetter(*attrs):
    _unsupported()


def itemgetter(*items):
    _unsupported()


def methodcaller(name, *args):
    _unsupported()


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
