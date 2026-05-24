#include <clovervm/native_module.h>

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

static clover_value math_float_result(clover_context *ctx, double result)
{
    if(errno == EDOM || (isnan(result) && errno != ERANGE))
    {
        return clover_raise_value_error(ctx, "math domain error");
    }
    if(errno == ERANGE && isinf(result))
    {
        return clover_raise_overflow_error(ctx, "math range error");
    }
    return clover_float_from_double(ctx, result);
}

static clover_value math_unary(clover_context *ctx, clover_value value,
                               double (*func)(double), int can_overflow)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    errno = 0;
    double result = func(number);
    if(isnan(result) && !isnan(number))
    {
        errno = EDOM;
    }
    if(can_overflow && isinf(result) && isfinite(number))
    {
        errno = ERANGE;
    }
    return math_float_result(ctx, result);
}

static clover_value math_binary(clover_context *ctx, clover_value left,
                                clover_value right,
                                double (*func)(double, double),
                                int can_overflow)
{
    double left_number;
    double right_number;
    if(clover_float_as_double(ctx, left, &left_number) != CLOVER_STATUS_OK ||
       clover_float_as_double(ctx, right, &right_number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    errno = 0;
    double result = func(left_number, right_number);
    if(isnan(result) && !isnan(left_number) && !isnan(right_number))
    {
        errno = EDOM;
    }
    if(can_overflow && isinf(result) && isfinite(left_number) &&
       isfinite(right_number))
    {
        errno = ERANGE;
    }
    return math_float_result(ctx, result);
}

#define MATH_UNARY_WRAPPER(wrapper_name, c_func, can_overflow)                 \
    static clover_value wrapper_name(clover_context *ctx, clover_value value)  \
    {                                                                          \
        return math_unary(ctx, value, c_func, can_overflow);                   \
    }

#define MATH_BINARY_WRAPPER(wrapper_name, c_func, can_overflow)                \
    static clover_value wrapper_name(clover_context *ctx, clover_value left,   \
                                     clover_value right)                       \
    {                                                                          \
        return math_binary(ctx, left, right, c_func, can_overflow);            \
    }

MATH_UNARY_WRAPPER(math_acos, acos, 0)
MATH_UNARY_WRAPPER(math_acosh, acosh, 0)
MATH_UNARY_WRAPPER(math_asin, asin, 0)
MATH_UNARY_WRAPPER(math_asinh, asinh, 0)
MATH_UNARY_WRAPPER(math_atan, atan, 0)
MATH_UNARY_WRAPPER(math_atanh, atanh, 0)
MATH_UNARY_WRAPPER(math_cbrt, cbrt, 0)
MATH_UNARY_WRAPPER(math_cos, cos, 0)
MATH_UNARY_WRAPPER(math_cosh, cosh, 1)
MATH_UNARY_WRAPPER(math_erf, erf, 0)
MATH_UNARY_WRAPPER(math_erfc, erfc, 0)
MATH_UNARY_WRAPPER(math_exp, exp, 1)
MATH_UNARY_WRAPPER(math_exp2, exp2, 1)
MATH_UNARY_WRAPPER(math_expm1, expm1, 1)
MATH_UNARY_WRAPPER(math_fabs, fabs, 0)
MATH_UNARY_WRAPPER(math_gamma, tgamma, 1)
MATH_UNARY_WRAPPER(math_lgamma, lgamma, 1)
MATH_UNARY_WRAPPER(math_log, log, 0)
MATH_UNARY_WRAPPER(math_log10, log10, 0)
MATH_UNARY_WRAPPER(math_log1p, log1p, 0)
MATH_UNARY_WRAPPER(math_log2, log2, 0)
MATH_UNARY_WRAPPER(math_sin, sin, 0)
MATH_UNARY_WRAPPER(math_sinh, sinh, 1)
MATH_UNARY_WRAPPER(math_sqrt, sqrt, 0)
MATH_UNARY_WRAPPER(math_tan, tan, 0)
MATH_UNARY_WRAPPER(math_tanh, tanh, 0)

MATH_BINARY_WRAPPER(math_atan2, atan2, 0)
MATH_BINARY_WRAPPER(math_copysign, copysign, 0)
MATH_BINARY_WRAPPER(math_fmod, fmod, 0)
MATH_BINARY_WRAPPER(math_pow, pow, 1)
MATH_BINARY_WRAPPER(math_remainder, remainder, 0)

static clover_value math_ceil(clover_context *ctx, clover_value value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(isnan(number))
    {
        return clover_raise_value_error(ctx,
                                        "cannot convert float NaN to integer");
    }
    if(isinf(number))
    {
        return clover_raise_overflow_error(
            ctx, "cannot convert float infinity to integer");
    }
    return clover_int_from_int64(ctx, (int64_t)ceil(number));
}

static clover_value math_floor(clover_context *ctx, clover_value value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(isnan(number))
    {
        return clover_raise_value_error(ctx,
                                        "cannot convert float NaN to integer");
    }
    if(isinf(number))
    {
        return clover_raise_overflow_error(
            ctx, "cannot convert float infinity to integer");
    }
    return clover_int_from_int64(ctx, (int64_t)floor(number));
}

static clover_value math_trunc(clover_context *ctx, clover_value value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(isnan(number))
    {
        return clover_raise_value_error(ctx,
                                        "cannot convert float NaN to integer");
    }
    if(isinf(number))
    {
        return clover_raise_overflow_error(
            ctx, "cannot convert float infinity to integer");
    }
    return clover_int_from_int64(ctx, (int64_t)trunc(number));
}

static clover_value math_fma(clover_context *ctx, clover_value x,
                             clover_value y, clover_value z)
{
    double x_number;
    double y_number;
    double z_number;
    if(clover_float_as_double(ctx, x, &x_number) != CLOVER_STATUS_OK ||
       clover_float_as_double(ctx, y, &y_number) != CLOVER_STATUS_OK ||
       clover_float_as_double(ctx, z, &z_number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    errno = 0;
    double result = fma(x_number, y_number, z_number);
    if(isinf(result) && isfinite(x_number) && isfinite(y_number) &&
       isfinite(z_number))
    {
        errno = ERANGE;
    }
    return math_float_result(ctx, result);
}

static clover_value math_frexp(clover_context *ctx, clover_value value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    int exponent;
    double mantissa = frexp(number, &exponent);
    clover_value items[] = {clover_float_from_double(ctx, mantissa),
                            clover_int_from_int64(ctx, exponent)};
    return clover_tuple_from_array(ctx, items, 2);
}

static clover_value math_ldexp(clover_context *ctx, clover_value x,
                               clover_value i)
{
    double number;
    int64_t exponent;
    if(clover_float_as_double(ctx, x, &number) != CLOVER_STATUS_OK ||
       clover_int_as_int64(ctx, i, &exponent) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(exponent > INT_MAX)
    {
        exponent = INT_MAX;
    }
    else if(exponent < INT_MIN)
    {
        exponent = INT_MIN;
    }
    errno = 0;
    double result = ldexp(number, (int)exponent);
    if(isinf(result) && isfinite(number))
    {
        errno = ERANGE;
    }
    return math_float_result(ctx, result);
}

static clover_value math_modf(clover_context *ctx, clover_value value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    double integer_part;
    double fractional_part = modf(number, &integer_part);
    clover_value items[] = {clover_float_from_double(ctx, fractional_part),
                            clover_float_from_double(ctx, integer_part)};
    return clover_tuple_from_array(ctx, items, 2);
}

static clover_value math_nextafter(clover_context *ctx, clover_value x,
                                   clover_value y)
{
    double x_number;
    double y_number;
    if(clover_float_as_double(ctx, x, &x_number) != CLOVER_STATUS_OK ||
       clover_float_as_double(ctx, y, &y_number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_float_from_double(ctx, nextafter(x_number, y_number));
}

static clover_value math_ulp(clover_context *ctx, clover_value value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(isnan(number))
    {
        return clover_float_from_double(ctx, number);
    }
    if(isinf(number))
    {
        return clover_float_from_double(ctx, INFINITY);
    }
    number = fabs(number);
    if(number == 0.0)
    {
        return clover_float_from_double(ctx, DBL_TRUE_MIN);
    }
    double next = nextafter(number, INFINITY);
    if(isinf(next))
    {
        next = nextafter(number, -INFINITY);
        return clover_float_from_double(ctx, number - next);
    }
    return clover_float_from_double(ctx, next - number);
}

#define ADD_FUNCTION_1(name, fn, doc)                                          \
    do                                                                         \
    {                                                                          \
        if(clover_module_add_function_1(builder, name, fn, doc) !=             \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

#define ADD_FUNCTION_2(name, fn, doc)                                          \
    do                                                                         \
    {                                                                          \
        if(clover_module_add_function_2(builder, name, fn, doc) !=             \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

#define ADD_FUNCTION_3(name, fn, doc)                                          \
    do                                                                         \
    {                                                                          \
        if(clover_module_add_function_3(builder, name, fn, doc) !=             \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

CL_NATIVE_MODULE_EXPORT clover_status clover_module_init__math(
    clover_context *ctx, clover_native_module_builder *builder)
{
    ADD_FUNCTION_1("acos", math_acos,
                   "Return the arc cosine of x, in radians.");
    ADD_FUNCTION_1("acosh", math_acosh,
                   "Return the inverse hyperbolic cosine of x.");
    ADD_FUNCTION_1("asin", math_asin, "Return the arc sine of x, in radians.");
    ADD_FUNCTION_1("asinh", math_asinh,
                   "Return the inverse hyperbolic sine of x.");
    ADD_FUNCTION_1("atan", math_atan,
                   "Return the arc tangent of x, in radians.");
    ADD_FUNCTION_2("atan2", math_atan2, "Return atan(y / x), in radians.");
    ADD_FUNCTION_1("atanh", math_atanh,
                   "Return the inverse hyperbolic tangent of x.");
    ADD_FUNCTION_1("cbrt", math_cbrt, "Return the cube root of x.");
    ADD_FUNCTION_1("ceil", math_ceil, "Return the ceiling of x.");
    ADD_FUNCTION_2("copysign", math_copysign,
                   "Return a float with x's magnitude and y's sign.");
    ADD_FUNCTION_1("cos", math_cos, "Return the cosine of x.");
    ADD_FUNCTION_1("cosh", math_cosh, "Return the hyperbolic cosine of x.");
    ADD_FUNCTION_1("erf", math_erf, "Return the error function at x.");
    ADD_FUNCTION_1("erfc", math_erfc,
                   "Return the complementary error function at x.");
    ADD_FUNCTION_1("exp", math_exp, "Return e raised to the power of x.");
    ADD_FUNCTION_1("exp2", math_exp2, "Return 2 raised to the power of x.");
    ADD_FUNCTION_1("expm1", math_expm1, "Return exp(x) - 1.");
    ADD_FUNCTION_1("fabs", math_fabs, "Return the absolute value of x.");
    ADD_FUNCTION_1("floor", math_floor, "Return the floor of x.");
    ADD_FUNCTION_3("fma", math_fma, "Return fused multiply-add of x, y, z.");
    ADD_FUNCTION_2("fmod", math_fmod, "Return fmod(x, y).");
    ADD_FUNCTION_1("frexp", math_frexp,
                   "Return the mantissa and exponent of x.");
    ADD_FUNCTION_1("gamma", math_gamma, "Return the gamma function at x.");
    ADD_FUNCTION_2("ldexp", math_ldexp, "Return x * (2**i).");
    ADD_FUNCTION_1("lgamma", math_lgamma, "Return log(abs(gamma(x))).");
    ADD_FUNCTION_1("log", math_log, "Return the natural logarithm of x.");
    ADD_FUNCTION_1("log10", math_log10, "Return the base 10 logarithm of x.");
    ADD_FUNCTION_1("log1p", math_log1p, "Return log(1 + x).");
    ADD_FUNCTION_1("log2", math_log2, "Return the base 2 logarithm of x.");
    ADD_FUNCTION_1("modf", math_modf,
                   "Return the fractional and integer parts of x.");
    ADD_FUNCTION_2("nextafter", math_nextafter,
                   "Return the next float after x toward y.");
    ADD_FUNCTION_2("pow", math_pow, "Return x raised to the power y.");
    ADD_FUNCTION_2("remainder", math_remainder, "Return IEEE remainder.");
    ADD_FUNCTION_1("sin", math_sin, "Return the sine of x.");
    ADD_FUNCTION_1("sinh", math_sinh, "Return the hyperbolic sine of x.");
    ADD_FUNCTION_1("sqrt", math_sqrt, "Return the square root of x.");
    ADD_FUNCTION_1("tan", math_tan, "Return the tangent of x.");
    ADD_FUNCTION_1("tanh", math_tanh, "Return the hyperbolic tangent of x.");
    ADD_FUNCTION_1("trunc", math_trunc, "Truncate x toward zero.");
    ADD_FUNCTION_1("ulp", math_ulp, "Return the value of the ulp of x.");
    return CLOVER_STATUS_OK;
}
