# Python test set -- accurate math summation and norms
#
# Portions adapted from CPython v3.14.5, Lib/test/test_math.py.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import math


def raises(error, function, /, *args, **kwargs):
    seen = False
    try:
        function(*args, **kwargs)
    except error:
        seen = True
    assert seen


def positive_zero(value):
    assert value == 0.0
    assert math.copysign(1.0, value) == 1.0


def zeros(n):
    values = []
    for i in range(n):
        values.append(0.0)
    return values


def test_finite():
    # Exact assertions pin cancellation, partial-tail rounding and the historical
    # large/tiny norm failures; an absolute isclose tolerance would hide the latter.
    two53 = math.ldexp(1.0, 53)
    for case in (
        ([], 0.0),
        ([1e16, 1.0, -1e16], 1.0),
        ([1e100, 1.0, -1e100, 1e-100, 1e50, -1, -1e50], 1e-100),
        ([two53, -0.5, -math.ldexp(1.0, -54)], two53 - 1.0),
        ([two53, 1.0, math.ldexp(1.0, -100)], two53 + 2.0),
        ([two53 + 10.0, 1.0, math.ldexp(1.0, -100)], two53 + 12.0),
        ([two53 - 4.0, 0.5, math.ldexp(1.0, -54)], two53 - 3.0),
        ([1e16, 1.0, 1e-16], 10000000000000002.0),
        ([1e308, -1e308, 1e308], 1e308),
        ([5e-324, 5e-324, -5e-324], 5e-324),
        ([True, False, 2**100, -2**100], 1.0),
    ):
        assert math.fsum(case[0]) == case[1]
        assert math.fsum(iter(case[0])) == case[1]

    for values in ([], [0.0], [-0.0], [1.0, -1.0]):
        positive_zero(math.fsum(values))

    assert math.hypot(1e308, 1e308) == 1.4142135623730951e308
    assert math.hypot(1e-300, 1e-300) == 1.414213562373095e-300
    assert math.dist([1e308, 1e308], [0, 0]) == 1.4142135623730951e308
    assert math.dist([1e-300, 1e-300], [0, 0]) == 1.414213562373095e-300
    assert math.hypot(True, False, True, True, True) == 2.0
    assert math.hypot(2**100, 0) == float(2**100)
    assert math.dist([2**100 + 1], [2**100]) == 0.0  # convert before subtracting
    assert math.dist([True, False], [False, False]) == 1.0
    assert math.hypot(-10.5) == 10.5
    assert math.dist(iter([1, 2, 3]), iter([4, 2, -1])) == 5.0
    for result in (math.hypot(), math.hypot(-0.0), math.hypot(0.0, -0.0),
                   math.dist([], []), math.dist([-0.0], [0.0]),
                   math.dist([0.0], [-0.0])):
        positive_zero(result)

    # Exact 3/4/5 triangles across the normal and subnormal range.
    for exponent in (-1074, -1073, -1023, -1022, -1000, -500, 0, 500, 1000, 1021):
        scale = math.ldexp(1.0, exponent)
        assert math.hypot(3 * scale, 4 * scale) == 5 * scale
        assert math.dist([3 * scale, 0], [0, 4 * scale]) == 5 * scale
        assert math.dist([0, 4 * scale], [3 * scale, 0]) == 5 * scale
    assert math.hypot(5e-324, 5e-324) == 5e-324
    assert math.hypot(1.7e308, 1.7e308) == math.inf
    assert math.dist([1e308], [-1e308]) == math.inf
    assert math.dist([1.7e308, 1.7e308], [0, 0]) == math.inf


def test_nonfinite():
    for values in ([math.inf], [-math.inf], [math.nan, math.inf],
                   [-math.inf, math.nan], [math.nan, -math.inf, math.inf]):
        assert math.hypot(*values) == math.inf
        assert math.dist(values, zeros(len(values))) == math.inf
    for values in ([math.nan], [1, math.nan], [math.nan, math.nan]):
        assert math.isnan(math.hypot(*values))
        assert math.isnan(math.dist(values, zeros(len(values))))
    assert math.isnan(math.dist([math.inf], [math.inf]))
    assert math.dist([math.inf, math.inf], [math.inf, 0]) == math.inf
    assert math.dist([1e308, math.nan], [-1e308, 0]) == math.inf
    assert math.fsum([1.0, math.inf]) == math.inf
    assert math.fsum([-math.inf, 1.0]) == -math.inf
    assert math.isnan(math.fsum([math.inf, math.nan]))
    assert math.isnan(math.fsum([math.nan, 1.0]))
    raises(TypeError, math.fsum, [math.inf, -math.inf, None])
    raises(OverflowError, math.fsum, [1e308, 1e308, None])
    raises(ValueError, math.fsum, [math.inf, -math.inf])
    raises(ValueError, math.fsum, [math.nan, math.inf, -math.inf])
    raises(OverflowError, math.fsum, [1e308, 1e308, -1e308])
    raises(OverflowError, math.fsum, [math.inf, 1e308, 1e308])


def test_arguments():
    # Arity, positional-only binding, conversion and dimension-error precedence.
    raises(TypeError, math.fsum)
    raises(TypeError, math.fsum, [], [])
    raises(TypeError, math.fsum, seq=[])
    raises(TypeError, math.hypot, coordinates=[])
    raises(TypeError, math.dist, [])
    raises(TypeError, math.dist, [], [], [])
    raises(TypeError, math.dist, p=[], q=[])
    raises(TypeError, math.fsum, 1)
    raises(TypeError, math.dist, 1, [])
    raises(TypeError, math.dist, [], 1)
    for bad in ("1.0", b"1.0", None, []):
        raises(TypeError, math.fsum, [bad])
        raises(TypeError, math.hypot, bad)
        raises(TypeError, math.dist, [bad], [0])
        raises(TypeError, math.dist, [0], [bad])
        raises(TypeError, math.hypot, math.inf, bad)
        raises(TypeError, math.fsum, [math.nan, bad])
    raises(TypeError, math.dist, [math.inf, None], [0, 0])
    raises(TypeError, math.dist, [0, 0], [math.inf, None])
    raises(ValueError, math.dist, [None], [])
    raises(ValueError, math.dist, [], [None])
    for function in (math.fsum, math.hypot):
        if function is math.fsum:
            raises(OverflowError, function, [2**2000])
        else:
            raises(OverflowError, function, 2**2000)
    raises(OverflowError, math.dist, [2**2000], [0])
    raises(OverflowError, math.dist, [0], [2**2000])


class Broken:
    def __init__(self):
        self.started = False

    def __iter__(self):
        return self

    def __next__(self):
        if not self.started:
            self.started = True
            return 1.0
        raise RuntimeError("iterator failed")


def test_iterators():
    raises(RuntimeError, math.fsum, Broken())
    raises(RuntimeError, math.dist, Broken(), [])
    raises(RuntimeError, math.dist, [], Broken())
    # Both inputs are materialized before numeric conversion or dimension checking.
    raises(RuntimeError, math.dist, [None], Broken())


class FloatLike:
    def __float__(self):
        raise RuntimeError("custom conversion")


def test_unsupported_conversion():
    # General conversion protocols remain outside the supported builtin slice.
    raises(TypeError, math.fsum, [FloatLike()])
    raises(TypeError, math.hypot, FloatLike())
    raises(TypeError, math.dist, [FloatLike()], [0])


# Independent oracle fixtures: exact Fraction sums rounded once to float;
# norms use exact Fraction sums of binary64 squares followed by Decimal sqrt.
# Norm results were stable at both 1000 and 1200 decimal digits. The fixture
# generators are the explicit formulas below (no random seed or libm oracle).
# A one-ULP norm bound admits faithful rounding, not spurious zero/inf results.

def sum_vector(n, phase):
    values = []
    for i in range(n):
        sign = 1.0
        if (i + phase) % 3 == 0:
            sign = -1.0
        values.append(math.ldexp(sign * (i % 17 + 1) / 16.0,
                                 (i * 37 + phase * 53) % 1800 - 900))
    return values


def test_sum_oracles():
    for case in (
        (40, 0, -1.0797393106838204e+163),
        (47, 1, 1.951954375502332e+257),
        (54, 2, 1.1808302949499833e+262),
        (61, 3, 7.093798635901802e+266),
        (68, 4, 2.7675710037526654e+260),
        (75, 5, 1.6122269626840267e+265),
        (82, 6, 9.245154294816484e+269),
        (89, 7, 1.5474229961003624e+267),
        (96, 8, -1.650920434390576e+268),
        (103, 9, -2.7407464755516944e+266),
        (110, 10, 1.2897827234695524e+266),
        (117, 11, 7.054476987080956e+264),
    ):
        values = sum_vector(case[0], case[1])
        assert math.fsum(values) == case[2]
        values.reverse()
        assert math.fsum(values) == case[2]


def norm_vector(kind, n, exponent):
    values = []
    if kind == 0:
        values.append(1.0)
        for i in range(n):
            values.append(math.ldexp(1.0, -26))
    else:
        for i in range(n):
            values.append(math.ldexp(((i * 37) % 101 - 50) / 16.0,
                                     (i % 9) - 4))
    scaled = []
    for value in values:
        scaled.append(math.ldexp(value, exponent))
    return scaled


def test_norm_oracles():
    for case in (
        (0, 1000, 0, 1.000000000000111),
        (0, 1000, 1000, 1.0715086071863863e+301),
        (0, 1000, -1000, 9.332636185033225e-302),
        (1, 37, 0, 68.5078429027259),
        (1, 257, 0, 180.97572092164836),
        (1, 1000, 0, 354.84275694506164),
        (1, 37, 1000, 7.340674333003544e+302),
        (1, 37, -1000, 6.393587736324803e-300),
        (1, 37, -1070, 5.415e-321),
        (1, 37, -1023, 7.621750517278675e-307),
    ):
        values = norm_vector(case[0], case[1], case[2])
        expected = case[3]
        actual = math.hypot(*values)
        assert math.isfinite(actual)
        assert actual > 0.0
        assert abs(actual - expected) <= math.ulp(expected)
        origin = zeros(len(values))
        assert abs(math.dist(values, origin) - expected) <= math.ulp(expected)
        assert abs(math.dist(origin, values) - expected) <= math.ulp(expected)
        values.reverse()
        assert abs(math.hypot(*values) - expected) <= math.ulp(expected)


def test_rounded_differences():
    # Oracle uses binary64-rounded differences, then exact rational squares
    # and a 1200-digit Decimal sqrt, matching dist's subtraction boundary.
    p = [1e+16, 1.0, 1e-300, 1.23456789012345, -8.7654321098765]
    q = [9999999999999998.0, -1.0, -1e-300, -9.87654321098765, 2.3456789012345]
    expected = 15.96601320939158
    assert abs(math.dist(p, q) - expected) <= math.ulp(expected)
    assert abs(math.dist(q, p) - expected) <= math.ulp(expected)


def test_many_partials():
    # CPython's partial-array growth regression, with an exact Fraction oracle
    # rounded once on reference Python (no VM bigint oracle dependency).
    values = []
    for n in range(-1074, 972, 2):
        values.append(math.ldexp(1.0, n) - math.ldexp(1.0, n + 50)
                      + math.ldexp(1.0, n + 52))
    values.append(-math.ldexp(1.0, 1022))
    assert math.fsum(values) == 1.3305602063564798e292


def test_rounding():
    # Historical CPython hard rounding cases, converted from hex to decimal
    # literals because float.fromhex is not part of the current VM surface.
    for case in (
        (572330659.1077356, 1180450261.5893514, 1311878501.7832494),
        (712049370.5674208, 791476409.2831447, 1064635718.251647),
        (1037023154.6745788, 794204895.5538545, 1306207655.5635877),
    ):
        assert math.hypot(case[0], case[1]) == case[2]
        assert math.hypot(case[1], case[0]) == case[2]
        assert math.dist([case[0], case[1]], [0, 0]) == case[2]


def exact_norm(values, expected):
    # Exercise both public paths and both accumulation orders.
    origin = zeros(len(values))
    assert math.hypot(*values) == expected
    assert math.dist(values, origin) == expected
    assert math.dist(origin, values) == expected
    values.reverse()
    assert math.hypot(*values) == expected
    assert math.dist(values, origin) == expected


def test_sqrt_residual_rounding():
    # Exact Fraction sum of binary64 squares, then Decimal sqrt at both
    # 1000 and 1200 digits, rounds to this result. Dropping the differential
    # sqrt correction produces 1.7853342861047612 (one ULP too low).
    exact_norm([1.0144399298253672, 0.7490780790438063, 1.263808598409246],
               1.7853342861047614)


def test_product_residual_rounding():
    # Same independent oracle construction. Dropping the FMA product
    # residuals produces 3.3277757461494226 (one ULP too low).
    exact_norm([1.5666519221384132, 1.0182011033490879, 1.5474239367017493,
                1.9013476394664506, 1.2543189381799513], 3.327775746149423)


def test_maximum_float_mixed_scales():
    # Binary64 DBL_MAX and its predecessor, using literals rather than sys.
    # The independently rounded norms equal the dominant coordinate. Scaling
    # the tiny coordinate can underflow, but must not disturb that result.
    maximum = 1.7976931348623157e308
    previous = 1.7976931348623155e308
    exact_norm([maximum, 5e-324], maximum)
    exact_norm([maximum, 1e292], maximum)
    exact_norm([-maximum, -1e292], maximum)
    exact_norm([previous, 1e-300], previous)


def test_subnormal_scaling_boundary():
    # frexp's exponent changes from -1024 to -1023 at 2**-1024. Probe the
    # adjacent floats on both sides, where the normalization branch changes.
    below = 5.562684646268e-309
    boundary = 5.562684646268003e-309
    above = 5.56268464626801e-309
    assert math.ldexp(1.0, -1024) == boundary
    assert math.nextafter(boundary, 0.0) == below
    assert math.nextafter(boundary, math.inf) == above
    for value in (below, boundary, above):
        exact_norm([value, 5e-324], value)
    exact_norm([below, below], 7.86682406995679e-309)
    exact_norm([boundary, boundary], 7.866824069956793e-309)
    # For the pair just above the boundary, CPython's rescaling double-rounds
    # to one ULP below the independently rounded 1200-digit Decimal oracle.
    # Preserve the agreed one-ULP accuracy bound instead of pinning that error.
    expected = 7.8668240699568e-309
    values = [above, above]
    origin = zeros(2)
    assert abs(math.hypot(*values) - expected) <= math.ulp(expected)
    assert abs(math.dist(values, origin) - expected) <= math.ulp(expected)


test_finite()
test_nonfinite()
test_arguments()
test_iterators()
test_unsupported_conversion()
test_sum_oracles()
test_norm_oracles()
test_rounding()
test_many_partials()
test_rounded_differences()
test_sqrt_residual_rounding()
test_product_residual_rounding()
test_maximum_float_mixed_scales()
test_subnormal_scaling_boundary()
