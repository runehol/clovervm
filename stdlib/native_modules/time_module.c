#define _POSIX_C_SOURCE 200809L

#include <clovervm/native_module.h>

#include <errno.h>
#include <math.h>
#include <time.h>

static clover_value seconds_from_timespec(clover_context *ctx,
                                          struct timespec ts)
{
    return clover_float_from_double(ctx, (double)ts.tv_sec +
                                             (double)ts.tv_nsec / 1000000000.0);
}

static clover_value time_time(clover_context *ctx)
{
    struct timespec ts;
    if(clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
        return clover_raise_value_error(ctx, "clock_gettime failed");
    }
    return seconds_from_timespec(ctx, ts);
}

static clover_value time_monotonic(clover_context *ctx)
{
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return clover_raise_value_error(ctx, "clock_gettime failed");
    }
    return seconds_from_timespec(ctx, ts);
}

static clover_value time_sleep(clover_context *ctx, clover_value seconds)
{
    double seconds_double;
    if(clover_float_as_double(ctx, seconds, &seconds_double) !=
       CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(seconds_double < 0.0)
    {
        return clover_raise_value_error(ctx,
                                        "sleep length must be non-negative");
    }
    if(!isfinite(seconds_double))
    {
        return clover_raise_value_error(ctx, "sleep length must be finite");
    }

    struct timespec remaining;
    remaining.tv_sec = (time_t)seconds_double;
    remaining.tv_nsec =
        (long)((seconds_double - (double)remaining.tv_sec) * 1000000000.0);
    if(remaining.tv_nsec >= 1000000000L)
    {
        remaining.tv_sec += 1;
        remaining.tv_nsec -= 1000000000L;
    }
    while(nanosleep(&remaining, &remaining) != 0)
    {
        if(errno != EINTR)
        {
            return clover_raise_value_error(ctx, "sleep failed");
        }
    }
    return clover_none(ctx);
}

CL_NATIVE_MODULE_EXPORT clover_status clover_module_init__time(
    clover_context *ctx, clover_native_module_builder *builder)
{
    (void)ctx;
    if(clover_module_add_function_0(builder, "time", time_time,
                                    "Return the current time in seconds.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_0(builder, "monotonic", time_monotonic,
                                    "Return a monotonic clock in seconds.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_module_add_function_1(
        builder, "sleep", time_sleep,
        "Delay execution for the given number of seconds.");
}
