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


def _as_builtin_int(value):
    if not isinstance(value, int):
        raise TypeError
    return int(value)


def _require_nonnegative(value):
    if value < 0:
        raise ValueError


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
    x = _as_real(x)
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


def _log(x):
    if isinstance(x, int):
        # Read the builtin payload, bypassing subclass arithmetic/conversion.
        x = int.__int__(x)
        if x <= 0:
            raise ValueError("math domain error")
        try:
            number = float(x)
        except OverflowError:
            # Integer true division rounds the normalized ratio to binary64
            # without first converting the entire integer to a float.
            exponent = int.bit_length(x)
            mantissa = x / (1 << exponent)
            if mantissa == 1.0:
                mantissa = 0.5
                exponent = exponent + 1
            return _math.log(mantissa) + _math.log(2.0) * exponent
        return _math.log(number)
    return _math.log(x)


def log(x, base=None):
    if base is None:
        return _log(x)
    return _log(x) / _log(base)


def nextafter(x, y, steps=None):
    if steps is None:
        return _math.nextafter(x, y)
    steps = _as_builtin_int(steps)
    _require_nonnegative(steps)
    result = x
    for _ in range(steps):
        result = _math.nextafter(result, y)
    return result


def factorial(n):
    n = _as_builtin_int(n)
    _require_nonnegative(n)
    result = 1
    for i in range(2, n + 1):
        result = result * i
    return result


def comb(n, k):
    n = _as_builtin_int(n)
    k = _as_builtin_int(k)
    _require_nonnegative(n)
    _require_nonnegative(k)
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
    n = _as_builtin_int(n)
    if k is None:
        k = n
    else:
        k = _as_builtin_int(k)
    _require_nonnegative(n)
    _require_nonnegative(k)
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
        value = _as_builtin_int(value)
        value = _abs(value)
        while value != 0:
            tmp = result % value
            result = value
            value = tmp
    return result


def lcm(*integers):
    result = 1
    for value in integers:
        value = _as_builtin_int(value)
        value = _abs(value)
        if result == 0 or value == 0:
            result = 0
        else:
            common = gcd(result, value)
            quotient = result // common
            result = quotient * value
    return result


def isqrt(n):
    n = _as_builtin_int(n)
    _require_nonnegative(n)
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


# The partials summation and compensated vector norm below are adapted from
# CPython v3.14.5, Modules/mathmodule.c (math_fsum and vector_norm).
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# They assume binary64 operations with round-to-nearest, ties-to-even.


def _as_real(value):
    # float() also accepts strings/bytes, unlike math's numeric conversion.
    # Custom __float__/__index__ dispatch is not supported by the VM yet.
    if not isinstance(value, int) and not isinstance(value, float):
        raise TypeError("must be a real number")
    return float(value)


def fsum(seq, /):
    partials = []
    special_sum = 0.0
    inf_sum = 0.0
    for item in seq:
        x = _as_real(item)
        saved = x
        remaining = []
        for y in partials:
            if _abs(x) < _abs(y):
                temporary = x
                x = y
                y = temporary
            hi = x + y
            yr = hi - x
            lo = y - yr
            if lo != 0.0:
                remaining.append(lo)
            x = hi
        partials = remaining
        if x != 0.0:
            if not isfinite(x):
                if isfinite(saved):
                    raise OverflowError("intermediate overflow in fsum")
                if isinf(saved):
                    inf_sum = inf_sum + saved
                special_sum = special_sum + saved
                partials = []
            else:
                partials.append(x)

    if special_sum != 0.0:
        if isnan(inf_sum):
            raise ValueError("-inf + inf in fsum")
        return special_sum

    hi = 0.0
    lo = 0.0
    if len(partials) > 0:
        hi = partials.pop()
        while len(partials) > 0:
            x = hi
            y = partials.pop()
            hi = x + y
            yr = hi - x
            lo = y - yr
            if lo != 0.0:
                break
        # Resolve a halfway result using the sign of the remaining tail.
        if len(partials) > 0:
            tail = partials[-1]
            if (lo < 0.0 and tail < 0.0) or (lo > 0.0 and tail > 0.0):
                y = lo * 2.0
                x = hi + y
                yr = x - hi
                if y == yr:
                    hi = x
    return hi


def _vector_norm(coordinates):
    maximum = 0.0
    found_nan = False
    for x in coordinates:
        if isnan(x):
            found_nan = True
        x = _abs(x)
        if x > maximum:
            maximum = x
    if isinf(maximum):
        return maximum
    if found_nan:
        return nan
    if maximum == 0.0 or len(coordinates) <= 1:
        return maximum

    exponent = frexp(maximum)[1]
    if exponent < -1023:
        # Scaling by 2**-exponent would overflow for tiny subnormals.
        normal_min = 2.2250738585072014e-308
        normalized = []
        for x in coordinates:
            normalized.append(x / normal_min)
        return normal_min * _vector_norm(normalized)

    scale = ldexp(1.0, -exponent)
    csum = 1.0
    frac1 = 0.0
    frac2 = 0.0
    for x in coordinates:
        x = x * scale
        product = x * x
        product_lo = fma(x, x, -product)
        total = csum + product
        total_lo = (csum - total) + product
        csum = total
        frac1 = frac1 + product_lo
        frac2 = frac2 + total_lo

    h = sqrt(csum - 1.0 + (frac1 + frac2))
    # Correct the rounded square root using the residual of its square.
    product = -h * h
    product_lo = fma(-h, h, -product)
    total = csum + product
    total_lo = (csum - total) + product
    csum = total
    frac1 = frac1 + product_lo
    frac2 = frac2 + total_lo
    residual = csum - 1.0 + (frac1 + frac2)
    h = h + residual / (2.0 * h)
    # True norm overflow returns infinity, rather than a libm range error.
    return h / scale


def hypot(*coordinates):
    converted = []
    for coordinate in coordinates:
        converted.append(_as_real(coordinate))
    return _vector_norm(converted)


def dist(p, q, /):
    # Materialize both iterables before checking dimensions or converting.
    # list(iterable) is supported; tuple(iterable) is not yet supported.
    p = list(p)
    q = list(q)
    if len(p) != len(q):
        raise ValueError("both points must have the same number of dimensions")
    differences = []
    for i in range(len(p)):
        px = _as_real(p[i])
        qx = _as_real(q[i])
        differences.append(px - qx)
    return _vector_norm(differences)


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
