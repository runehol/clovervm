import _math


e = 2.718281828459045
pi = 3.141592653589793
tau = 6.283185307179586
inf = 1e1000
nan = inf - inf

acos = _math.acos
acosh = _math.acosh
asin = _math.asin
asinh = _math.asinh
atan = _math.atan
atan2 = _math.atan2
atanh = _math.atanh
cbrt = _math.cbrt
ceil = _math.ceil
copysign = _math.copysign
cos = _math.cos
cosh = _math.cosh
erf = _math.erf
erfc = _math.erfc
exp = _math.exp
exp2 = _math.exp2
expm1 = _math.expm1
fabs = _math.fabs
floor = _math.floor
fma = _math.fma
fmod = _math.fmod
frexp = _math.frexp
gamma = _math.gamma
ldexp = _math.ldexp
lgamma = _math.lgamma
log10 = _math.log10
log1p = _math.log1p
log2 = _math.log2
modf = _math.modf
pow = _math.pow
remainder = _math.remainder
sin = _math.sin
sinh = _math.sinh
sqrt = _math.sqrt
tan = _math.tan
tanh = _math.tanh
trunc = _math.trunc
ulp = _math.ulp


def _abs(x):
    if x < 0:
        return -x
    return x


def _check_nonnegative_int(name, value):
    if value < 0:
        raise ValueError
    return value


def degrees(x):
    return x * 180.0 / pi


def radians(x):
    return x * pi / 180.0


def isfinite(x):
    return not isinf(x) and not isnan(x)


def isinf(x):
    if x == inf:
        return True
    if x == -inf:
        return True
    return False


def isnan(x):
    return x != x


def isclose(a, b, rel_tol=1e-09, abs_tol=0.0):
    if rel_tol < 0.0:
        raise ValueError
    if abs_tol < 0.0:
        raise ValueError
    if a == b:
        return True
    if isinf(a):
        return False
    if isinf(b):
        return False
    if isnan(a):
        return False
    if isnan(b):
        return False
    diff = _abs(b - a)
    if diff <= _abs(rel_tol * b):
        return True
    if diff <= _abs(rel_tol * a):
        return True
    if diff <= abs_tol:
        return True
    return False


def log(x, base=None):
    if base is None:
        return _math.log(x)
    return _math.log(x) / _math.log(base)


def nextafter(x, y, steps=None):
    if steps is None:
        return _math.nextafter(x, y)
    if steps < 0:
        raise ValueError
    result = x
    for _ in range(steps):
        result = _math.nextafter(result, y)
    return result


def factorial(n):
    _check_nonnegative_int("n", n)
    result = 1
    for i in range(2, n + 1):
        result = result * i
    return result


def comb(n, k):
    _check_nonnegative_int("n", n)
    _check_nonnegative_int("k", k)
    if k > n:
        return 0
    other = n - k
    if other < k:
        k = other
    result = 1
    for i in range(1, k + 1):
        result = result * (n - k + i) // i
    return result


def perm(n, k=None):
    _check_nonnegative_int("n", n)
    if k is None:
        k = n
    _check_nonnegative_int("k", k)
    if k > n:
        return 0
    result = 1
    stop = n - k
    while n > stop:
        result = result * n
        n = n - 1
    return result


def gcd(*integers):
    result = 0
    for value in integers:
        value = _abs(value)
        while value:
            tmp = result % value
            result = value
            value = tmp
    return result


def lcm(*integers):
    result = 1
    for value in integers:
        value = _abs(value)
        if result == 0 or value == 0:
            result = 0
        else:
            common = gcd(result, value)
            quotient = result // common
            result = quotient * value
    return result


def isqrt(n):
    _check_nonnegative_int("n", n)
    if n < 2:
        return n
    low = 1
    high = n
    while low <= high:
        mid = (low + high) // 2
        square = mid * mid
        if square == n:
            return mid
        if square < n:
            low = mid + 1
        else:
            high = mid - 1
    return high


def prod(iterable, start=1):
    result = start
    for item in iterable:
        result = result * item
    return result


def fsum(seq):
    total = 0.0
    for item in seq:
        total = total + item
    return total


def hypot(*coordinates):
    total = 0.0
    for coordinate in coordinates:
        total = total + coordinate * coordinate
    return sqrt(total)


def dist(p, q):
    pit = iter(p)
    qit = iter(q)
    sentinel = []
    total = 0.0
    while True:
        px = next(pit, sentinel)
        qx = next(qit, sentinel)
        if px is sentinel:
            if qx is sentinel:
                return sqrt(total)
            raise ValueError
        if qx is sentinel:
            if px is sentinel:
                return sqrt(total)
            raise ValueError
        delta = px - qx
        total = total + delta * delta


def sumprod(p, q):
    pit = iter(p)
    qit = iter(q)
    sentinel = []
    total = 0
    while True:
        px = next(pit, sentinel)
        qx = next(qit, sentinel)
        if px is sentinel:
            if qx is sentinel:
                return total
            raise ValueError
        if qx is sentinel:
            if px is sentinel:
                return total
            raise ValueError
        total = total + px * qx
