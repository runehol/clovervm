# Python test set -- itertools count
#
# Portions adapted from CPython's Lib/test/test_itertools.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

from itertools import count, islice


def raises(kind, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as caught:
        return caught
    raise AssertionError("expected exception")


def test_values_and_signatures():
    values = count()
    assert iter(values) is values
    assert list(islice(values, 4)) == [0, 1, 2, 3]
    assert list(islice(count(start=2, step=3), 4)) == [2, 5, 8, 11]
    assert list(islice(count(step=-1), 4)) == [0, -1, -2, -3]
    assert list(islice(count(5, 0), 3)) == [5, 5, 5]
    assert list(islice(count(-3, 2), 4)) == [-3, -1, 1, 3]
    assert list(islice(count(2.25, 0.25), 3)) == [2.25, 2.5, 2.75]
    assert list(islice(count(0.0, 0.1), 7)) == [0.0, 0.1, 0.2, 0.30000000000000004, 0.4, 0.5, 0.6]
    raises(TypeError, count, 1, 2, 3)
    raises(TypeError, count, unknown=1)
    raises(TypeError, count, 1, start=2)
    raises(TypeError, count, 1, 2, step=3)
    for bad in (None, "1", [], (), object()):
        assert raises(TypeError, count, bad).args == ("a number is required",)
        assert raises(TypeError, count, 0, bad).args == ("a number is required",)


def test_bigints_and_boundaries():
    for start in (2**61 - 1, 2**63 - 2, 2**63 - 1, 2**63,
                  -(2**63), -(2**63) - 1, 10**40):
        assert list(islice(count(start), 3)) == [start, start + 1, start + 2]
        assert list(islice(count(start, -2), 3)) == [start, start - 2, start - 4]
    stride = 10**40
    assert list(islice(count(1, stride), 3)) == [1, 1 + stride, 1 + 2 * stride]
    assert list(islice(count(-(2**63), 2**63), 3)) == [-(2**63), 0, 2**63]


def test_types_and_special_floats():
    assert next(count(True)).__class__ is int
    assert next(count(False, True)).__class__ is int
    values = count(False, False)
    assert next(values) is False
    assert next(values).__class__ is int
    values = count(0, 1.0)
    assert next(values).__class__ is int
    assert next(values).__class__ is float
    values = count(0.0)
    assert next(values).__class__ is float
    assert next(values).__class__ is float
    values = count(-0.0, 0)
    assert repr(next(values)) == "-0.0"
    assert repr(next(values)) == "0.0"
    infinity = float("inf")
    values = count(infinity, -infinity)
    assert next(values) == infinity
    nan = next(values)
    assert nan != nan
    values = count(1, float("nan"))
    assert next(values) == 1
    nan = next(values)
    assert nan != nan
    # Mixed heap-int/float conversion and overflow are covered in
    # itertools_count_mixed_numbers.py and the host reference checks.


error = ValueError("addition failed")
additions = []
fail_addition = True


class Integer(int):
    def __int__(self):
        raise AssertionError("count must read the integer payload")

    def __index__(self):
        raise AssertionError("count must read the integer payload")

    def __eq__(self, other):
        raise AssertionError("unit-step detection must read the payload")

    def __add__(self, other):
        additions.append("add")
        if fail_addition:
            raise error
        return 17

    def __radd__(self, other):
        additions.append("radd")
        if fail_addition:
            raise error
        return 19


class Real(float):
    pass


stopping_error = StopIteration("addition stopped")
stop_addition = True


class StoppingInteger(int):
    def __add__(self, other):
        if stop_addition:
            raise stopping_error
        return 5


def test_subclasses_and_retry():
    global additions, fail_addition
    additions = []
    fail_addition = True
    assert list(islice(count(Integer(2), Integer(1)), 3)) == [2, 3, 4]
    assert additions == []
    first = Integer(2)
    values = count(first, 0)
    assert raises(ValueError, next, values) is error
    assert raises(ValueError, next, values) is error
    assert additions == ["add", "add"]
    fail_addition = False
    assert next(values) is first
    assert next(values) == 17
    # At SSIZE_MAX the normalized start is added to the original step object.
    additions = []
    fail_addition = True
    values = count(Integer(2**63 - 2), Integer(1))
    assert next(values) == 2**63 - 2
    assert raises(ValueError, next, values) is error
    assert raises(ValueError, next, values) is error
    assert additions == ["radd", "radd"]
    # Out-of-platform starts remain objects rather than entering the fast path.
    values = count(Integer(2**63), 1)
    assert raises(ValueError, next, values) is error
    assert additions[-1] == "add"
    value = Real(2.5)
    values = count(value, 0.5)
    assert next(values) is value
    assert next(values) == 3.0


def test_hot_iteration():
    assert list(islice(count(), 240)) == list(range(240))
    assert list(islice(count(0.5, 0.5), 240))[-1] == 120.0


def test_addition_stop_recovery():
    global stop_addition
    stop_addition = True
    first = StoppingInteger(2)
    values = count(first, 0)
    assert raises(StopIteration, next, values) is stopping_error
    assert next(values, "done") == "done"
    stop_addition = False
    assert next(values) is first
    assert next(values) == 5


test_values_and_signatures()
test_bigints_and_boundaries()
test_types_and_special_floats()
test_subclasses_and_retry()
test_hot_iteration()
test_addition_stop_recovery()
