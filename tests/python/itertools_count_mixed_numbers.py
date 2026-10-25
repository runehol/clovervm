# Mixed count behavior checked against CPython 3.14.5.
# The public module delegates to real builtin arithmetic, including conversion
# overflow before returning an item; it must not eagerly convert the start.

from itertools import count, islice


for case in (
    (1.0, 2**60, [1.0, 1152921504606846976.0, 2305843009213693952.0]),
    (2**60, 1.0, [2**60, 1152921504606846976.0, 1152921504606846976.0]),
    (1.0, -(2**60), [1.0, -1152921504606846976.0, -2305843009213693952.0]),
    (-(2**60), 1.0, [-(2**60), -1152921504606846976.0, -1152921504606846976.0]),
):
    values = count(case[0], case[1])
    for expected in case[2]:
        actual = next(values)
        assert actual.__class__ is expected.__class__
        assert actual == expected


for case in ((1.0, 10**400), (10**400, 1.0)):
    values = count(case[0], case[1])
    for attempt in (0, 1):
        try:
            next(values)
            assert False
        except OverflowError:
            pass


# Exercise the mixed path repeatedly without relying on multiplication for the
# counter implementation. These power-of-two increments are exactly representable.
values = list(islice(count(0.5, 2**60), 240))
assert values[0] == 0.5
assert values[-1] == 275548239601036427264.0
