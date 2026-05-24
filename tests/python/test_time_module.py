# Python test set -- time module
#
# Portions adapted from CPython's Lib/test/test_time.py
# at CPython v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import time


epoch = time.gmtime(0)
assert epoch[0] == 1970
assert epoch[1] == 1
assert epoch[2] == 1
assert epoch[3] == 0
assert epoch[4] == 0
assert epoch[5] == 0
assert epoch[6] == 3
assert epoch[7] == 1
assert epoch[8] == 0
assert len(time.asctime(epoch)) == 24
assert len(time.strftime("%Y-%m-%d %H:%M:%S", epoch)) == 19

now = time.time()
assert now > 0.0
assert time.clock_gettime(time.CLOCK_REALTIME) > 0.0
assert time.clock_getres(time.CLOCK_REALTIME) > 0.0

before = time.monotonic()
before_ns = time.monotonic_ns()
time.sleep(0)
after = time.monotonic()
after_ns = time.monotonic_ns()
assert after >= before
assert after_ns >= before_ns

perf_before = time.perf_counter()
perf_after = time.perf_counter()
assert perf_after >= perf_before
assert time.perf_counter_ns() >= 0

assert time.process_time() >= 0.0
assert time.process_time_ns() >= 0
assert time.thread_time() >= 0.0
assert time.thread_time_ns() >= 0

local_epoch = time.localtime(0)
assert time.mktime(local_epoch) == 0.0
assert len(time.ctime(0)) == len(time.asctime(local_epoch))

info = time.get_clock_info("monotonic")
assert info["monotonic"]
assert not info["adjustable"]
assert info["resolution"] > 0.0

try:
    time.sleep(-1)
    assert False
except ValueError:
    pass

try:
    time.asctime((1970, 13, 1, 0, 0, 0, 3, 1, 0))
    assert False
except ValueError:
    pass
