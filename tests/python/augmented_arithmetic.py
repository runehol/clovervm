# Original regressions for augmented arithmetic, checked against CPython 3.14.5.
# No builtin in-place methods are added by this core dispatch slice.

def do_add(a, b):
    a += b
    return a

def immediate_add(a):
    a += 3
    return a

def do_sub(a, b):
    a -= b
    return a

def immediate_sub(a):
    a -= 3
    return a

def do_mul(a, b):
    a *= b
    return a

def immediate_mul(a):
    a *= 3
    return a

def do_matmul(a, b):
    a @= b
    return a

def immediate_matmul(a):
    a @= 3
    return a

def do_truediv(a, b):
    a /= b
    return a

def immediate_truediv(a):
    a /= 3
    return a

def do_floordiv(a, b):
    a //= b
    return a

def immediate_floordiv(a):
    a //= 3
    return a

def do_pow(a, b):
    a **= b
    return a

def immediate_pow(a):
    a **= 3
    return a

def do_lshift(a, b):
    a <<= b
    return a

def immediate_lshift(a):
    a <<= 3
    return a

def do_rshift(a, b):
    a >>= b
    return a

def immediate_rshift(a):
    a >>= 3
    return a

def do_mod(a, b):
    a %= b
    return a

def immediate_mod(a):
    a %= 3
    return a

def do_or(a, b):
    a |= b
    return a

def immediate_or(a):
    a |= 3
    return a

def do_and(a, b):
    a &= b
    return a

def immediate_and(a):
    a &= 3
    return a

def do_xor(a, b):
    a ^= b
    return a

def immediate_xor(a):
    a ^= 3
    return a

class InPlace:
    def __init__(self):
        self.seen = []

    def __iadd__(self, other):
        self.seen.append(("add", other))
        return self

    def __isub__(self, other):
        self.seen.append(("sub", other))
        return self

    def __imul__(self, other):
        self.seen.append(("mul", other))
        return self

    def __imatmul__(self, other):
        self.seen.append(("matmul", other))
        return self

    def __itruediv__(self, other):
        self.seen.append(("truediv", other))
        return self

    def __ifloordiv__(self, other):
        self.seen.append(("floordiv", other))
        return self

    def __ipow__(self, other):
        self.seen.append(("pow", other))
        return self

    def __ilshift__(self, other):
        self.seen.append(("lshift", other))
        return self

    def __irshift__(self, other):
        self.seen.append(("rshift", other))
        return self

    def __imod__(self, other):
        self.seen.append(("mod", other))
        return self

    def __ior__(self, other):
        self.seen.append(("or", other))
        return self

    def __iand__(self, other):
        self.seen.append(("and", other))
        return self

    def __ixor__(self, other):
        self.seen.append(("xor", other))
        return self

class Replacement:
    def __iadd__(self, other):
        return ("add", other)

    def __isub__(self, other):
        return ("sub", other)

    def __imul__(self, other):
        return ("mul", other)

    def __imatmul__(self, other):
        return ("matmul", other)

    def __itruediv__(self, other):
        return ("truediv", other)

    def __ifloordiv__(self, other):
        return ("floordiv", other)

    def __ipow__(self, other):
        return ("pow", other)

    def __ilshift__(self, other):
        return ("lshift", other)

    def __irshift__(self, other):
        return ("rshift", other)

    def __imod__(self, other):
        return ("mod", other)

    def __ior__(self, other):
        return ("or", other)

    def __iand__(self, other):
        return ("and", other)

    def __ixor__(self, other):
        return ("xor", other)

def check_operator_cases():
    a = InPlace()
    assert do_add(a, 3) is a
    assert immediate_add(a) is a
    assert a.seen == [("add", 3), ("add", 3)]
    assert do_add(Replacement(), 3) == ("add", 3)
    assert immediate_add(Replacement()) == ("add", 3)
    assert do_add(10, 3) == 13
    assert immediate_add(10) == 13
    a = InPlace()
    assert do_sub(a, 3) is a
    assert immediate_sub(a) is a
    assert a.seen == [("sub", 3), ("sub", 3)]
    assert do_sub(Replacement(), 3) == ("sub", 3)
    assert immediate_sub(Replacement()) == ("sub", 3)
    assert do_sub(10, 3) == 7
    assert immediate_sub(10) == 7
    a = InPlace()
    assert do_mul(a, 3) is a
    assert immediate_mul(a) is a
    assert a.seen == [("mul", 3), ("mul", 3)]
    assert do_mul(Replacement(), 3) == ("mul", 3)
    assert immediate_mul(Replacement()) == ("mul", 3)
    assert do_mul(10, 3) == 30
    assert immediate_mul(10) == 30
    a = InPlace()
    assert do_matmul(a, 3) is a
    assert immediate_matmul(a) is a
    assert a.seen == [("matmul", 3), ("matmul", 3)]
    assert do_matmul(Replacement(), 3) == ("matmul", 3)
    assert immediate_matmul(Replacement()) == ("matmul", 3)
    a = InPlace()
    assert do_truediv(a, 3) is a
    assert immediate_truediv(a) is a
    assert a.seen == [("truediv", 3), ("truediv", 3)]
    assert do_truediv(Replacement(), 3) == ("truediv", 3)
    assert immediate_truediv(Replacement()) == ("truediv", 3)
    assert do_truediv(10, 3) == 3.3333333333333335
    assert immediate_truediv(10) == 3.3333333333333335
    a = InPlace()
    assert do_floordiv(a, 3) is a
    assert immediate_floordiv(a) is a
    assert a.seen == [("floordiv", 3), ("floordiv", 3)]
    assert do_floordiv(Replacement(), 3) == ("floordiv", 3)
    assert immediate_floordiv(Replacement()) == ("floordiv", 3)
    assert do_floordiv(10, 3) == 3
    assert immediate_floordiv(10) == 3
    a = InPlace()
    assert do_pow(a, 3) is a
    assert immediate_pow(a) is a
    assert a.seen == [("pow", 3), ("pow", 3)]
    assert do_pow(Replacement(), 3) == ("pow", 3)
    assert immediate_pow(Replacement()) == ("pow", 3)
    assert do_pow(10, 3) == 1000
    assert immediate_pow(10) == 1000
    a = InPlace()
    assert do_lshift(a, 3) is a
    assert immediate_lshift(a) is a
    assert a.seen == [("lshift", 3), ("lshift", 3)]
    assert do_lshift(Replacement(), 3) == ("lshift", 3)
    assert immediate_lshift(Replacement()) == ("lshift", 3)
    assert do_lshift(10, 3) == 80
    assert immediate_lshift(10) == 80
    a = InPlace()
    assert do_rshift(a, 3) is a
    assert immediate_rshift(a) is a
    assert a.seen == [("rshift", 3), ("rshift", 3)]
    assert do_rshift(Replacement(), 3) == ("rshift", 3)
    assert immediate_rshift(Replacement()) == ("rshift", 3)
    assert do_rshift(10, 3) == 1
    assert immediate_rshift(10) == 1
    a = InPlace()
    assert do_mod(a, 3) is a
    assert immediate_mod(a) is a
    assert a.seen == [("mod", 3), ("mod", 3)]
    assert do_mod(Replacement(), 3) == ("mod", 3)
    assert immediate_mod(Replacement()) == ("mod", 3)
    assert do_mod(10, 3) == 1
    assert immediate_mod(10) == 1
    a = InPlace()
    assert do_or(a, 3) is a
    assert immediate_or(a) is a
    assert a.seen == [("or", 3), ("or", 3)]
    assert do_or(Replacement(), 3) == ("or", 3)
    assert immediate_or(Replacement()) == ("or", 3)
    assert do_or(10, 3) == 11
    assert immediate_or(10) == 11
    a = InPlace()
    assert do_and(a, 3) is a
    assert immediate_and(a) is a
    assert a.seen == [("and", 3), ("and", 3)]
    assert do_and(Replacement(), 3) == ("and", 3)
    assert immediate_and(Replacement()) == ("and", 3)
    assert do_and(10, 3) == 2
    assert immediate_and(10) == 2
    a = InPlace()
    assert do_xor(a, 3) is a
    assert immediate_xor(a) is a
    assert a.seen == [("xor", 3), ("xor", 3)]
    assert do_xor(Replacement(), 3) == ("xor", 3)
    assert immediate_xor(Replacement()) == ("xor", 3)
    assert do_xor(10, 3) == 9
    assert immediate_xor(10) == 9


check_operator_cases()

class Declines:
    def __iadd__(self, other):
        return NotImplemented
    def __add__(self, other):
        return ("fallback-add", other)
    def __isub__(self, other):
        return NotImplemented
    def __sub__(self, other):
        return ("fallback-sub", other)
    def __imul__(self, other):
        return NotImplemented
    def __mul__(self, other):
        return ("fallback-mul", other)
    def __imatmul__(self, other):
        return NotImplemented
    def __matmul__(self, other):
        return ("fallback-matmul", other)
    def __itruediv__(self, other):
        return NotImplemented
    def __truediv__(self, other):
        return ("fallback-truediv", other)
    def __ifloordiv__(self, other):
        return NotImplemented
    def __floordiv__(self, other):
        return ("fallback-floordiv", other)
    def __ipow__(self, other):
        return NotImplemented
    def __pow__(self, other):
        return ("fallback-pow", other)
    def __ilshift__(self, other):
        return NotImplemented
    def __lshift__(self, other):
        return ("fallback-lshift", other)
    def __irshift__(self, other):
        return NotImplemented
    def __rshift__(self, other):
        return ("fallback-rshift", other)
    def __imod__(self, other):
        return NotImplemented
    def __mod__(self, other):
        return ("fallback-mod", other)
    def __ior__(self, other):
        return NotImplemented
    def __or__(self, other):
        return ("fallback-or", other)
    def __iand__(self, other):
        return NotImplemented
    def __and__(self, other):
        return ("fallback-and", other)
    def __ixor__(self, other):
        return NotImplemented
    def __xor__(self, other):
        return ("fallback-xor", other)

def check_declining_cases():
    assert do_add(Declines(), 3) == ("fallback-add", 3)
    assert immediate_add(Declines()) == ("fallback-add", 3)
    assert do_sub(Declines(), 3) == ("fallback-sub", 3)
    assert immediate_sub(Declines()) == ("fallback-sub", 3)
    assert do_mul(Declines(), 3) == ("fallback-mul", 3)
    assert immediate_mul(Declines()) == ("fallback-mul", 3)
    assert do_matmul(Declines(), 3) == ("fallback-matmul", 3)
    assert immediate_matmul(Declines()) == ("fallback-matmul", 3)
    assert do_truediv(Declines(), 3) == ("fallback-truediv", 3)
    assert immediate_truediv(Declines()) == ("fallback-truediv", 3)
    assert do_floordiv(Declines(), 3) == ("fallback-floordiv", 3)
    assert immediate_floordiv(Declines()) == ("fallback-floordiv", 3)
    assert do_pow(Declines(), 3) == ("fallback-pow", 3)
    assert immediate_pow(Declines()) == ("fallback-pow", 3)
    assert do_lshift(Declines(), 3) == ("fallback-lshift", 3)
    assert immediate_lshift(Declines()) == ("fallback-lshift", 3)
    assert do_rshift(Declines(), 3) == ("fallback-rshift", 3)
    assert immediate_rshift(Declines()) == ("fallback-rshift", 3)
    assert do_mod(Declines(), 3) == ("fallback-mod", 3)
    assert immediate_mod(Declines()) == ("fallback-mod", 3)
    assert do_or(Declines(), 3) == ("fallback-or", 3)
    assert immediate_or(Declines()) == ("fallback-or", 3)
    assert do_and(Declines(), 3) == ("fallback-and", 3)
    assert immediate_and(Declines()) == ("fallback-and", 3)
    assert do_xor(Declines(), 3) == ("fallback-xor", 3)
    assert immediate_xor(Declines()) == ("fallback-xor", 3)


check_declining_cases()
