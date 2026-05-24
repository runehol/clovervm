import _time


CLOCK_REALTIME = _time.CLOCK_REALTIME
CLOCK_MONOTONIC = _time.CLOCK_MONOTONIC
CLOCK_PROCESS_CPUTIME_ID = _time.CLOCK_PROCESS_CPUTIME_ID
CLOCK_THREAD_CPUTIME_ID = _time.CLOCK_THREAD_CPUTIME_ID

time = _time.time
time_ns = _time.time_ns
monotonic = _time.monotonic
monotonic_ns = _time.monotonic_ns
perf_counter = _time.perf_counter
perf_counter_ns = _time.perf_counter_ns
process_time = _time.process_time
process_time_ns = _time.process_time_ns
thread_time = _time.thread_time
thread_time_ns = _time.thread_time_ns
sleep = _time.sleep
clock_gettime = _time.clock_gettime
clock_gettime_ns = _time.clock_gettime_ns
clock_getres = _time.clock_getres
mktime = _time.mktime
tzset = _time.tzset
timezone = _time.timezone
altzone = _time.altzone
daylight = _time.daylight
tzname = _time.tzname

def gmtime(seconds=None):
    if seconds is None:
        seconds = time()
    return _time._gmtime(seconds)


def localtime(seconds=None):
    if seconds is None:
        seconds = time()
    return _time._localtime(seconds)


def asctime(t=None):
    if t is None:
        t = localtime()
    return _time.asctime(t)


def ctime(seconds=None):
    return asctime(localtime(seconds))


def strftime(format, t=None):
    if t is None:
        t = localtime()
    return _time.strftime(format, t)


def get_clock_info(name):
    if name == "time":
        return {
            "implementation": "clock_gettime(CLOCK_REALTIME)",
            "monotonic": False,
            "adjustable": True,
            "resolution": clock_getres(CLOCK_REALTIME),
        }
    if name == "monotonic":
        return {
            "implementation": "clock_gettime(CLOCK_MONOTONIC)",
            "monotonic": True,
            "adjustable": False,
            "resolution": clock_getres(CLOCK_MONOTONIC),
        }
    if name == "perf_counter":
        return {
            "implementation": "clock_gettime(CLOCK_MONOTONIC)",
            "monotonic": True,
            "adjustable": False,
            "resolution": clock_getres(CLOCK_MONOTONIC),
        }
    if name == "process_time":
        return {
            "implementation": "clock_gettime(CLOCK_PROCESS_CPUTIME_ID)",
            "monotonic": True,
            "adjustable": False,
            "resolution": clock_getres(CLOCK_PROCESS_CPUTIME_ID),
        }
    if name == "thread_time":
        return {
            "implementation": "clock_gettime(CLOCK_THREAD_CPUTIME_ID)",
            "monotonic": True,
            "adjustable": False,
            "resolution": clock_getres(CLOCK_THREAD_CPUTIME_ID),
        }
    raise ValueError
