#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <clovervm/native_module.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const int64_t NS_PER_SECOND = 1000000000LL;

static clover_handle seconds_from_timespec(clover_context *ctx,
                                           struct timespec ts)
{
    return clover_float_from_double(ctx, (double)ts.tv_sec +
                                             (double)ts.tv_nsec / 1000000000.0);
}

static clover_handle ns_from_timespec(clover_context *ctx, struct timespec ts)
{
    if(ts.tv_sec > (INT64_MAX - ts.tv_nsec) / NS_PER_SECOND)
    {
        return clover_raise_overflow_error(ctx, "timestamp out of range");
    }
    if(ts.tv_sec < (INT64_MIN + ts.tv_nsec) / NS_PER_SECOND)
    {
        return clover_raise_overflow_error(ctx, "timestamp out of range");
    }
    return clover_int_from_int64(ctx, (int64_t)ts.tv_sec * NS_PER_SECOND +
                                          (int64_t)ts.tv_nsec);
}

static clover_status clock_id_from_value(clover_context *ctx,
                                         clover_handle value, clockid_t *out)
{
    int64_t raw;
    if(clover_int_as_int64(ctx, value, &raw) != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(raw < INT_MIN || raw > INT_MAX)
    {
        (void)clover_raise_value_error(ctx, "clock id out of range");
        return CLOVER_STATUS_ERROR;
    }
    *out = (clockid_t)raw;
    return CLOVER_STATUS_OK;
}

static clover_handle time_clock_gettime(clover_context *ctx,
                                        clover_handle clock_id)
{
    clockid_t id;
    struct timespec ts;
    if(clock_id_from_value(ctx, clock_id, &id) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(clock_gettime(id, &ts) != 0)
    {
        return clover_raise_value_error(ctx, "clock_gettime failed");
    }
    return seconds_from_timespec(ctx, ts);
}

static clover_handle time_clock_gettime_ns(clover_context *ctx,
                                           clover_handle clock_id)
{
    clockid_t id;
    struct timespec ts;
    if(clock_id_from_value(ctx, clock_id, &id) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(clock_gettime(id, &ts) != 0)
    {
        return clover_raise_value_error(ctx, "clock_gettime failed");
    }
    return ns_from_timespec(ctx, ts);
}

static clover_handle time_clock_getres(clover_context *ctx,
                                       clover_handle clock_id)
{
    clockid_t id;
    struct timespec ts;
    if(clock_id_from_value(ctx, clock_id, &id) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(clock_getres(id, &ts) != 0)
    {
        return clover_raise_value_error(ctx, "clock_getres failed");
    }
    return seconds_from_timespec(ctx, ts);
}

static clover_handle clock_seconds(clover_context *ctx, clockid_t clock_id)
{
    struct timespec ts;
    if(clock_gettime(clock_id, &ts) != 0)
    {
        return clover_raise_value_error(ctx, "clock_gettime failed");
    }
    return seconds_from_timespec(ctx, ts);
}

static clover_handle clock_nanoseconds(clover_context *ctx, clockid_t clock_id)
{
    struct timespec ts;
    if(clock_gettime(clock_id, &ts) != 0)
    {
        return clover_raise_value_error(ctx, "clock_gettime failed");
    }
    return ns_from_timespec(ctx, ts);
}

static clover_handle time_time(clover_context *ctx)
{
    return clock_seconds(ctx, CLOCK_REALTIME);
}

static clover_handle time_time_ns(clover_context *ctx)
{
    return clock_nanoseconds(ctx, CLOCK_REALTIME);
}

static clover_handle time_monotonic(clover_context *ctx)
{
    return clock_seconds(ctx, CLOCK_MONOTONIC);
}

static clover_handle time_monotonic_ns(clover_context *ctx)
{
    return clock_nanoseconds(ctx, CLOCK_MONOTONIC);
}

static clover_handle time_perf_counter(clover_context *ctx)
{
    return time_monotonic(ctx);
}

static clover_handle time_perf_counter_ns(clover_context *ctx)
{
    return time_monotonic_ns(ctx);
}

static clover_handle time_process_time(clover_context *ctx)
{
#ifdef CLOCK_PROCESS_CPUTIME_ID
    return clock_seconds(ctx, CLOCK_PROCESS_CPUTIME_ID);
#else
    clock_t ticks = clock();
    if(ticks == (clock_t)-1)
    {
        return clover_raise_value_error(ctx, "process time is unavailable");
    }
    return clover_float_from_double(ctx,
                                    (double)ticks / (double)CLOCKS_PER_SEC);
#endif
}

static clover_handle time_process_time_ns(clover_context *ctx)
{
#ifdef CLOCK_PROCESS_CPUTIME_ID
    return clock_nanoseconds(ctx, CLOCK_PROCESS_CPUTIME_ID);
#else
    clock_t ticks = clock();
    if(ticks == (clock_t)-1)
    {
        return clover_raise_value_error(ctx, "process time is unavailable");
    }
    double ns = ((double)ticks * 1000000000.0) / (double)CLOCKS_PER_SEC;
    if(ns > (double)INT64_MAX)
    {
        return clover_raise_overflow_error(ctx, "timestamp out of range");
    }
    return clover_int_from_int64(ctx, (int64_t)ns);
#endif
}

static clover_handle time_thread_time(clover_context *ctx)
{
#ifdef CLOCK_THREAD_CPUTIME_ID
    return clock_seconds(ctx, CLOCK_THREAD_CPUTIME_ID);
#else
    return time_process_time(ctx);
#endif
}

static clover_handle time_thread_time_ns(clover_context *ctx)
{
#ifdef CLOCK_THREAD_CPUTIME_ID
    return clock_nanoseconds(ctx, CLOCK_THREAD_CPUTIME_ID);
#else
    return time_process_time_ns(ctx);
#endif
}

static clover_handle time_sleep(clover_context *ctx, clover_handle seconds)
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

static clover_handle tm_to_tuple(clover_context *ctx, const struct tm *tm)
{
    clover_handle items[] = {
        clover_int_from_int64(ctx, tm->tm_year + 1900),
        clover_int_from_int64(ctx, tm->tm_mon + 1),
        clover_int_from_int64(ctx, tm->tm_mday),
        clover_int_from_int64(ctx, tm->tm_hour),
        clover_int_from_int64(ctx, tm->tm_min),
        clover_int_from_int64(ctx, tm->tm_sec),
        clover_int_from_int64(ctx, (tm->tm_wday + 6) % 7),
        clover_int_from_int64(ctx, tm->tm_yday + 1),
        clover_int_from_int64(ctx, tm->tm_isdst),
    };
    return clover_tuple_from_array(ctx, items, 9);
}

static clover_handle time_gmtime(clover_context *ctx, clover_handle seconds)
{
    double seconds_double;
    if(clover_float_as_double(ctx, seconds, &seconds_double) !=
       CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    time_t timestamp = (time_t)floor(seconds_double);
    struct tm tm;
    if(gmtime_r(&timestamp, &tm) == NULL)
    {
        return clover_raise_value_error(ctx, "gmtime failed");
    }
    tm.tm_isdst = 0;
    return tm_to_tuple(ctx, &tm);
}

static clover_handle time_localtime(clover_context *ctx, clover_handle seconds)
{
    double seconds_double;
    if(clover_float_as_double(ctx, seconds, &seconds_double) !=
       CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    time_t timestamp = (time_t)floor(seconds_double);
    struct tm tm;
    if(localtime_r(&timestamp, &tm) == NULL)
    {
        return clover_raise_value_error(ctx, "localtime failed");
    }
    return tm_to_tuple(ctx, &tm);
}

static clover_status int_from_tuple(clover_context *ctx, clover_handle tuple,
                                    size_t index, int *out)
{
    clover_handle item;
    int64_t value;
    if(clover_tuple_get_item(ctx, tuple, index, &item) != CLOVER_STATUS_OK ||
       clover_int_as_int64(ctx, item, &value) != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(value < INT_MIN || value > INT_MAX)
    {
        (void)clover_raise_value_error(ctx, "time tuple field out of range");
        return CLOVER_STATUS_ERROR;
    }
    *out = (int)value;
    return CLOVER_STATUS_OK;
}

static clover_status tm_from_tuple(clover_context *ctx, clover_handle tuple,
                                   struct tm *out)
{
    size_t size;
    if(clover_tuple_size(ctx, tuple, &size) != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(size < 9)
    {
        (void)clover_raise_value_error(ctx,
                                       "time tuple must have at least 9 items");
        return CLOVER_STATUS_ERROR;
    }

    int year;
    memset(out, 0, sizeof(*out));
    if(int_from_tuple(ctx, tuple, 0, &year) != CLOVER_STATUS_OK ||
       int_from_tuple(ctx, tuple, 1, &out->tm_mon) != CLOVER_STATUS_OK ||
       int_from_tuple(ctx, tuple, 2, &out->tm_mday) != CLOVER_STATUS_OK ||
       int_from_tuple(ctx, tuple, 3, &out->tm_hour) != CLOVER_STATUS_OK ||
       int_from_tuple(ctx, tuple, 4, &out->tm_min) != CLOVER_STATUS_OK ||
       int_from_tuple(ctx, tuple, 5, &out->tm_sec) != CLOVER_STATUS_OK ||
       int_from_tuple(ctx, tuple, 6, &out->tm_wday) != CLOVER_STATUS_OK ||
       int_from_tuple(ctx, tuple, 7, &out->tm_yday) != CLOVER_STATUS_OK ||
       int_from_tuple(ctx, tuple, 8, &out->tm_isdst) != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }

    if(year < INT_MIN + 1900)
    {
        (void)clover_raise_overflow_error(ctx, "year out of range");
        return CLOVER_STATUS_ERROR;
    }
    out->tm_year = year - 1900;
    out->tm_mon -= 1;
    out->tm_wday = (out->tm_wday + 1) % 7;
    out->tm_yday -= 1;
    return CLOVER_STATUS_OK;
}

static clover_status check_tm(clover_context *ctx, struct tm *tm)
{
    if(tm->tm_mon == -1)
    {
        tm->tm_mon = 0;
    }
    else if(tm->tm_mon < 0 || tm->tm_mon > 11)
    {
        (void)clover_raise_value_error(ctx, "month out of range");
        return CLOVER_STATUS_ERROR;
    }
    if(tm->tm_mday == 0)
    {
        tm->tm_mday = 1;
    }
    else if(tm->tm_mday < 0 || tm->tm_mday > 31)
    {
        (void)clover_raise_value_error(ctx, "day of month out of range");
        return CLOVER_STATUS_ERROR;
    }
    if(tm->tm_hour < 0 || tm->tm_hour > 23)
    {
        (void)clover_raise_value_error(ctx, "hour out of range");
        return CLOVER_STATUS_ERROR;
    }
    if(tm->tm_min < 0 || tm->tm_min > 59)
    {
        (void)clover_raise_value_error(ctx, "minute out of range");
        return CLOVER_STATUS_ERROR;
    }
    if(tm->tm_sec < 0 || tm->tm_sec > 61)
    {
        (void)clover_raise_value_error(ctx, "seconds out of range");
        return CLOVER_STATUS_ERROR;
    }
    if(tm->tm_wday < 0 || tm->tm_wday > 6)
    {
        (void)clover_raise_value_error(ctx, "day of week out of range");
        return CLOVER_STATUS_ERROR;
    }
    if(tm->tm_yday < 0 || tm->tm_yday > 365)
    {
        (void)clover_raise_value_error(ctx, "day of year out of range");
        return CLOVER_STATUS_ERROR;
    }
    return CLOVER_STATUS_OK;
}

static clover_handle time_asctime(clover_context *ctx, clover_handle tuple)
{
    struct tm tm;
    if(tm_from_tuple(ctx, tuple, &tm) != CLOVER_STATUS_OK ||
       check_tm(ctx, &tm) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }

    char buffer[64];
    if(strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Y", &tm) == 0)
    {
        return clover_raise_value_error(ctx, "asctime failed");
    }
    return clover_string_from_utf8(ctx, buffer);
}

static clover_handle time_mktime(clover_context *ctx, clover_handle tuple)
{
    struct tm tm;
    if(tm_from_tuple(ctx, tuple, &tm) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }

    errno = 0;
    time_t result = mktime(&tm);
    if(result == (time_t)-1 && errno != 0)
    {
        return clover_raise_value_error(ctx, "mktime failed");
    }
    return clover_float_from_double(ctx, (double)result);
}

static clover_handle time_strftime(clover_context *ctx, clover_handle format,
                                   clover_handle tuple)
{
    char format_buffer[256];
    size_t format_size;
    struct tm tm;
    if(clover_string_as_utf8(ctx, format, format_buffer, sizeof(format_buffer),
                             &format_size) != CLOVER_STATUS_OK ||
       tm_from_tuple(ctx, tuple, &tm) != CLOVER_STATUS_OK ||
       check_tm(ctx, &tm) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }

    char buffer[256];
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    size_t size = strftime(buffer, sizeof(buffer), format_buffer, &tm);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    if(size == 0)
    {
        return clover_raise_value_error(ctx, "strftime result is too large");
    }
    return clover_string_from_utf8(ctx, buffer);
}

static clover_handle time_tzset(clover_context *ctx)
{
    (void)ctx;
    tzset();
    return clover_none(ctx);
}

#define ADD_FUNCTION_0(name, fn, doc)                                          \
    do                                                                         \
    {                                                                          \
        if(clover_module_add_function_0(builder, name, fn, doc) !=             \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

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

#define ADD_INT(name, value)                                                   \
    do                                                                         \
    {                                                                          \
        if(clover_module_add_value(builder, name,                              \
                                   clover_int_from_int64(ctx, value)) !=       \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

CL_NATIVE_MODULE_EXPORT clover_status clover_module_init__time(
    clover_context *ctx, clover_native_module_builder *builder)
{
    tzset();

    ADD_FUNCTION_0("time", time_time,
                   "Return the current time in seconds since the Epoch.");
    ADD_FUNCTION_0("time_ns", time_time_ns,
                   "Return the current time in nanoseconds since the Epoch.");
    ADD_FUNCTION_0("monotonic", time_monotonic,
                   "Return a monotonic clock in seconds.");
    ADD_FUNCTION_0("monotonic_ns", time_monotonic_ns,
                   "Return a monotonic clock in nanoseconds.");
    ADD_FUNCTION_0("perf_counter", time_perf_counter,
                   "Return a performance counter in seconds.");
    ADD_FUNCTION_0("perf_counter_ns", time_perf_counter_ns,
                   "Return a performance counter in nanoseconds.");
    ADD_FUNCTION_0("process_time", time_process_time,
                   "Return process CPU time in seconds.");
    ADD_FUNCTION_0("process_time_ns", time_process_time_ns,
                   "Return process CPU time in nanoseconds.");
    ADD_FUNCTION_0("thread_time", time_thread_time,
                   "Return thread CPU time in seconds.");
    ADD_FUNCTION_0("thread_time_ns", time_thread_time_ns,
                   "Return thread CPU time in nanoseconds.");
    ADD_FUNCTION_0("tzset", time_tzset, "Reinitialize local timezone data.");
    ADD_FUNCTION_1("sleep", time_sleep,
                   "Delay execution for the given number of seconds.");
    ADD_FUNCTION_1("_gmtime", time_gmtime,
                   "Convert seconds since the Epoch to UTC tuple.");
    ADD_FUNCTION_1("_localtime", time_localtime,
                   "Convert seconds since the Epoch to local tuple.");
    ADD_FUNCTION_1("asctime", time_asctime,
                   "Convert a time tuple to a string.");
    ADD_FUNCTION_1("mktime", time_mktime,
                   "Convert a local time tuple to seconds since the Epoch.");
    ADD_FUNCTION_1("clock_gettime", time_clock_gettime,
                   "Return the time of the specified clock as a float.");
    ADD_FUNCTION_2("strftime", time_strftime,
                   "Format a time tuple according to a format string.");
    ADD_FUNCTION_1("clock_gettime_ns", time_clock_gettime_ns,
                   "Return the specified clock in nanoseconds.");
    ADD_FUNCTION_1("clock_getres", time_clock_getres,
                   "Return the resolution of the specified clock.");

#ifdef CLOCK_REALTIME
    ADD_INT("CLOCK_REALTIME", CLOCK_REALTIME);
#endif
#ifdef CLOCK_MONOTONIC
    ADD_INT("CLOCK_MONOTONIC", CLOCK_MONOTONIC);
#endif
#ifdef CLOCK_MONOTONIC_RAW
    ADD_INT("CLOCK_MONOTONIC_RAW", CLOCK_MONOTONIC_RAW);
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID
    ADD_INT("CLOCK_PROCESS_CPUTIME_ID", CLOCK_PROCESS_CPUTIME_ID);
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
    ADD_INT("CLOCK_THREAD_CPUTIME_ID", CLOCK_THREAD_CPUTIME_ID);
#endif
#ifdef CLOCK_UPTIME_RAW
    ADD_INT("CLOCK_UPTIME_RAW", CLOCK_UPTIME_RAW);
#endif
#ifdef CLOCK_UPTIME_RAW_APPROX
    ADD_INT("CLOCK_UPTIME_RAW_APPROX", CLOCK_UPTIME_RAW_APPROX);
#endif
#ifdef CLOCK_MONOTONIC_RAW_APPROX
    ADD_INT("CLOCK_MONOTONIC_RAW_APPROX", CLOCK_MONOTONIC_RAW_APPROX);
#endif

#if defined(__APPLE__) || defined(__unix__)
    ADD_INT("timezone", timezone);
#ifdef altzone
    ADD_INT("altzone", altzone);
#else
    ADD_INT("altzone", timezone - 3600);
#endif
    ADD_INT("daylight", daylight);
    clover_handle tz_items[] = {clover_string_from_utf8(ctx, tzname[0]),
                                clover_string_from_utf8(ctx, tzname[1])};
    if(clover_module_add_value(builder, "tzname",
                               clover_tuple_from_array(ctx, tz_items, 2)) !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
#endif

    return CLOVER_STATUS_OK;
}
