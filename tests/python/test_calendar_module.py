# Python test set -- calendar module
#
# Portions adapted from CPython's Lib/test/test_calendar.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import calendar

assert calendar.firstweekday() == calendar.MONDAY


def raises(error, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


class Integer(int):
    pass


def test_arithmetic():
    for year in (0, -400, 400, 1600, 2000, 2400, 10**40):
        assert calendar.isleap(year)
    for year in (-100, 100, 1700, 1800, 1900, 2001, 2100):
        assert not calendar.isleap(year)
    assert calendar.isleap(2000.0)
    assert calendar.leapdays(1999.0, 2001.0) == 1.0
    assert calendar.leapdays(2010, 2010) == 0
    assert calendar.leapdays(2010, 2012) == 0
    assert calendar.leapdays(2012, 2013) == 1
    assert calendar.leapdays(1997, 2020) == 5
    assert calendar.leapdays(2001, 1999) == -1
    assert calendar.leapdays(0, 400) == 97
    assert calendar.leapdays(-400, 0) == 97
    assert calendar.leapdays(-1, 0) == 0
    assert calendar.leapdays(0, 1) == 1
    assert calendar.leapdays(-10**40, 10**40) == 485 * 10**37
    assert calendar.weekday(1, 1, 1) == calendar.MONDAY
    assert calendar.weekday(1970, 1, 1) == calendar.THURSDAY
    assert calendar.weekday(2000, 1, 1) == calendar.SATURDAY
    assert calendar.weekday(0, 1, 1) == calendar.SATURDAY
    assert calendar.weekday(-1, 1, 1) == calendar.FRIDAY
    assert calendar.weekday(10**40, 1, 1) == calendar.SATURDAY
    assert calendar.weekday(-10**40, 1, 1) == calendar.SATURDAY
    assert calendar.weekday(True, True, True) == calendar.MONDAY
    assert calendar.weekday(Integer(2000), Integer(1), Integer(1)) == 5
    assert calendar.monthrange(2004, 1) == (3, 31)
    assert calendar.monthrange(2004, 2) == (6, 29)
    assert calendar.monthrange(2010, 2) == (0, 28)
    assert calendar.monthrange(2004, 12) == (2, 31)
    assert calendar.monthrange(0, 2)[1] == 29
    assert calendar.monthrange(-100, 2)[1] == 28


def test_errors():
    assert calendar.error is ValueError
    assert issubclass(calendar.IllegalMonthError, ValueError)
    assert issubclass(calendar.IllegalMonthError, IndexError)
    for month in (0, 13, -1, 65, 10**40):
        error = raises(calendar.IllegalMonthError, calendar.monthrange, 2004, month)
        assert error.month == month
        assert error.args == (month,)
        assert str(error) == "bad month number " + repr(month) + "; must be 1-12"
        raises(calendar.IllegalMonthError, calendar.monthcalendar, 2004, month)
    for value in (-1, 7, 123):
        error = raises(calendar.IllegalWeekdayError, calendar.setfirstweekday, value)
        assert error.weekday == value
        assert error.args == (value,)
        assert str(error) == ("bad weekday number " + repr(value)
                              + "; must be 0 (Monday) to 6 (Sunday)")
    for args in ((2000, 0, 1), (2000, 13, 1), (2000, 1, 0),
                 (1900, 2, 29), (-1, 2, 29), (2000, 2, 30), (2000, 4, 31)):
        raises(ValueError, calendar.weekday, args[0], args[1], args[2])
    for args in ((2000.0, 1, 1), (0.0, 1, 1), (2000, 1.0, 1),
                 (2000, 1, 1.0), (2000, 0, "x"), (2000, 13, 1.0)):
        raises(TypeError, calendar.weekday, args[0], args[1], args[2])
    for huge in (-10**40, -2147483649, 2147483648, 10**40):
        raises(OverflowError, calendar.weekday, 2000, huge, 1)
        raises(OverflowError, calendar.weekday, 2000, 1, huge)
    raises(TypeError, calendar.monthrange, 2000, 2.0)
    raises(TypeError, calendar.setfirstweekday, "flabber")
    raises(TypeError, calendar.setfirstweekday, None)
    # Month range validation happens before year conversion.
    raises(calendar.IllegalMonthError, calendar.monthrange, "x", 0)


def test_calls():
    assert calendar.isleap(year=2000)
    assert calendar.leapdays(y2=2001, y1=1999) == 1
    assert calendar.weekday(day=1, year=2000, month=1) == 5
    assert calendar.monthrange(month=2, year=2000) == (1, 29)
    calendar.setfirstweekday(firstweekday=0)
    assert calendar.monthcalendar(month=2, year=1999)[0] == [1, 2, 3, 4, 5, 6, 7]
    for function in (calendar.isleap, calendar.leapdays, calendar.weekday,
                     calendar.monthrange, calendar.monthcalendar, calendar.setfirstweekday):
        raises(TypeError, function)
        raises(TypeError, function, unexpected=1)
        raises(TypeError, function, 1, 2, 3, 4)
    raises(TypeError, calendar.firstweekday, 1)
    raises(TypeError, calendar.firstweekday, unexpected=1)
    raises(TypeError, calendar.isleap, 2000, year=2000)


def test_state_and_grids():
    assert calendar.firstweekday() == 0
    # This slice deliberately uses plain integers instead of CPython enums.
    assert calendar.MONDAY.__class__ is int
    assert calendar.JANUARY.__class__ is int
    assert calendar.weekday(2000, 1, 1).__class__ is int
    assert calendar.monthrange(2000, 1)[0].__class__ is int
    assert (calendar.MONDAY, calendar.TUESDAY, calendar.WEDNESDAY,
            calendar.THURSDAY, calendar.FRIDAY, calendar.SATURDAY,
            calendar.SUNDAY) == (0, 1, 2, 3, 4, 5, 6)
    assert (calendar.JANUARY, calendar.FEBRUARY, calendar.MARCH,
            calendar.APRIL, calendar.MAY, calendar.JUNE, calendar.JULY,
            calendar.AUGUST, calendar.SEPTEMBER, calendar.OCTOBER,
            calendar.NOVEMBER, calendar.DECEMBER) == (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)
    for start in range(7):
        assert calendar.setfirstweekday(start) is None
        assert calendar.firstweekday() == start
        raises(calendar.IllegalWeekdayError, calendar.setfirstweekday, 7)
        assert calendar.firstweekday() == start
        assert calendar.monthrange(2000, 2) == (1, 29)
    calendar.setfirstweekday(calendar.MONDAY)
    grid = calendar.monthcalendar(1999, 2)
    assert len(grid) == 4
    assert grid[0] == [1, 2, 3, 4, 5, 6, 7]
    assert grid[3] == [22, 23, 24, 25, 26, 27, 28]
    grid[0][0] = 99
    assert grid[1][0] == 8
    assert calendar.monthcalendar(1999, 2)[0][0] == 1
    calendar.setfirstweekday(calendar.SUNDAY)
    grid = calendar.monthcalendar(1999, 2)
    assert len(grid) == 5
    assert grid[0] == [0, 1, 2, 3, 4, 5, 6]
    assert grid[4] == [28, 0, 0, 0, 0, 0, 0]
    calendar.setfirstweekday(True)
    assert calendar.firstweekday() == 1
    calendar.setfirstweekday(1.5)
    assert calendar.firstweekday() == 1.5
    raises(TypeError, calendar.monthcalendar, 2000, 2)
    calendar.setfirstweekday(0.0)
    raises(TypeError, calendar.monthcalendar, 2000, 2)
    calendar.setfirstweekday(0)


class LyingYear(int):
    def __ge__(self, other):
        return True

    def __le__(self, other):
        return True


class BadRemainder(int):
    def __mod__(self, other):
        return -5000


def test_converted_year_validation():
    for year in (LyingYear(0), LyingYear(10000), BadRemainder(-1)):
        error = raises(ValueError, calendar.weekday, year, 1, 1)
        expected = int(year)
        if expected == -1:
            expected = -3000
        assert str(error) == "year must be in 1..9999, not " + str(expected)
        # Convert every argument before checking any date ranges.
        raises(TypeError, calendar.weekday, year, 1.0, 1)
        raises(TypeError, calendar.weekday, year, 13, "x")
        raises(OverflowError, calendar.weekday, year, 13, 2**31)
        raises(OverflowError, calendar.weekday, year, 2**31, 1)
        # With valid conversions the year error must precede month/day errors.
        error = raises(ValueError, calendar.weekday, year, 0, 0)
        assert str(error) == "year must be in 1..9999, not " + str(expected)
        raises(ValueError, calendar.monthrange, year, 1)
        raises(ValueError, calendar.monthcalendar, year, 1)
        # monthrange still validates its month before calling weekday.
        raises(calendar.IllegalMonthError, calendar.monthrange, year, 0)


def test_exception_constructor_calls():
    for constructor in (calendar.IllegalMonthError, calendar.IllegalWeekdayError):
        positional = constructor(13)
        assert positional.args == (13,)
        if constructor is calendar.IllegalMonthError:
            keyword = constructor(month=13)
            assert keyword.month == positional.month == 13
            raises(TypeError, constructor, 13, month=14)
        else:
            keyword = constructor(weekday=13)
            assert keyword.weekday == positional.weekday == 13
            raises(TypeError, constructor, 13, weekday=14)
        assert keyword.args == ()
        assert str(keyword) == str(positional)
        assert repr(keyword) == constructor.__name__ + "()"
        assert repr(positional) == constructor.__name__ + "(13)"
        raises(TypeError, constructor)
        raises(TypeError, constructor, 1, 2)
        raises(TypeError, constructor, unexpected=13)


class MonthSubclass(calendar.IllegalMonthError):
    def __init__(self, month=13):
        self.month = month
        self.seen_args = self.args


class WeekdaySubclass(calendar.IllegalWeekdayError):
    def __init__(self, weekday=7):
        self.weekday = weekday
        self.seen_args = self.args


def test_exception_reinitialization():
    month = calendar.IllegalMonthError(13)
    month.__init__(month=14)
    assert month.args == (13,)
    assert month.month == 14
    assert repr(month) == "IllegalMonthError(13)"
    assert str(month) == "bad month number 14; must be 1-12"
    month.__init__(15)
    assert month.args == (13,)
    assert month.month == 15
    month = calendar.IllegalMonthError(month=13)
    month.__init__(14)
    assert month.args == ()
    assert month.month == 14
    weekday = calendar.IllegalWeekdayError(7)
    weekday.__init__(weekday=8)
    assert weekday.args == (7,)
    assert weekday.weekday == 8
    weekday.__init__(9)
    assert weekday.args == (7,)
    assert weekday.weekday == 9
    weekday = calendar.IllegalWeekdayError(weekday=7)
    weekday.__init__(8)
    assert weekday.args == ()
    assert weekday.weekday == 8
    month = MonthSubclass(16)
    assert month.args == month.seen_args == (16,)
    assert MonthSubclass(month=17).args == ()
    assert MonthSubclass().args == ()
    weekday = WeekdaySubclass(9)
    assert weekday.args == weekday.seen_args == (9,)
    assert WeekdaySubclass(weekday=10).args == ()
    assert WeekdaySubclass().args == ()


test_arithmetic()
test_errors()
test_calls()
test_state_and_grids()

test_converted_year_validation()
test_exception_constructor_calls()

test_exception_reinitialization()
