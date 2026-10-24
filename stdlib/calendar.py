"""A bounded proleptic Gregorian calendar, without datetime or locale support.

Weekday and month constants/results are plain integers. Custom __index__
conversion, enum classes, and calendar formatting are not implemented.
"""

# Portions adapted from CPython's Lib/calendar.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

from operator import index as _index


MONDAY = 0
TUESDAY = 1
WEDNESDAY = 2
THURSDAY = 3
FRIDAY = 4
SATURDAY = 5
SUNDAY = 6

JANUARY = 1
FEBRUARY = 2
MARCH = 3
APRIL = 4
MAY = 5
JUNE = 6
JULY = 7
AUGUST = 8
SEPTEMBER = 9
OCTOBER = 10
NOVEMBER = 11
DECEMBER = 12

error = ValueError


class IllegalMonthError(ValueError, IndexError):
    def __init__(self, month):
        self.month = month

    def __str__(self):
        return "bad month number " + repr(self.month) + "; must be 1-12"


class IllegalWeekdayError(ValueError):
    def __init__(self, weekday):
        self.weekday = weekday

    def __str__(self):
        return ("bad weekday number " + repr(self.weekday)
                + "; must be 0 (Monday) to 6 (Sunday)")


__all__ = [
    "IllegalMonthError", "IllegalWeekdayError", "setfirstweekday",
    "firstweekday", "isleap", "leapdays", "weekday", "monthrange",
    "monthcalendar", "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY",
    "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER",
    "DECEMBER", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY",
    "SATURDAY", "SUNDAY",
]

_mdays = (0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
_firstweekday = MONDAY


def isleap(year):
    """Return whether year is a leap year."""
    return year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)


def leapdays(y1, y2):
    """Count leap years in [y1, y2), assuming y1 <= y2."""
    y1 -= 1
    y2 -= 1
    return (y2 // 4 - y1 // 4) - (y2 // 100 - y1 // 100) + (y2 // 400 - y1 // 400)


def _date_component(value):
    # datetime.date converts all three arguments to C int before range checks.
    value = _index(value)
    if value < -2147483648 or value > 2147483647:
        raise OverflowError("calendar date component does not fit a C int")
    return value


def _monthlen(year, month):
    return _mdays[month] + (month == FEBRUARY and isleap(year))


def weekday(year, month, day):
    """Return the weekday, Monday=0, for a valid Gregorian date."""
    # Match calendar's mapping before datetime's argument conversion. This
    # keeps arbitrarily large, zero, and negative years within a 400-year cycle.
    if not 1 <= year <= 9999:
        year = 2000 + year % 400
    year = _date_component(year)
    month = _date_component(month)
    day = _date_component(day)
    # Int subclasses can override the comparisons or remainder used above.
    # Validate their normalized, exact-int result after all conversions.
    if not 1 <= year <= 9999:
        raise ValueError("year must be in 1..9999, not " + str(year))
    if not 1 <= month <= 12:
        raise ValueError("month must be in 1..12, not " + str(month))
    ndays = _monthlen(year, month)
    if not 1 <= day <= ndays:
        raise ValueError("day " + str(day) + " must be in range 1.."
                         + str(ndays) + " for month " + str(month)
                         + " in year " + str(year))
    # 0001-01-01 was Monday. Count complete years and preceding months.
    previous = year - 1
    days = 365 * previous + previous // 4 - previous // 100 + previous // 400
    for preceding in range(1, month):
        days += _monthlen(year, preceding)
    return (days + day - 1) % 7


def monthrange(year, month):
    """Return (weekday of first day, number of days) for the month."""
    if not 1 <= month <= 12:
        raise IllegalMonthError(month)
    day1 = weekday(year, month, 1)
    return day1, _monthlen(year, month)


def firstweekday():
    """Return the current first weekday, Monday=0."""
    return _firstweekday % 7


def setfirstweekday(firstweekday):
    """Set the first weekday, validating the inclusive range 0 through 6."""
    # Like CPython, accept in-range floats here; grid generation requires
    # integral padding counts and rejects them there.
    if not MONDAY <= firstweekday <= SUNDAY:
        raise IllegalWeekdayError(firstweekday)
    global _firstweekday
    _firstweekday = firstweekday


def monthcalendar(year, month):
    """Return fresh seven-day week lists, with zeroes outside this month."""
    bounds = monthrange(year, month)
    before = (bounds[0] - firstweekday()) % 7
    days = []
    for unused in range(before):
        days.append(0)
    for day in range(1, bounds[1] + 1):
        days.append(day)
    after = (firstweekday() - bounds[0] - bounds[1]) % 7
    for unused in range(after):
        days.append(0)
    weeks = []
    for start in range(0, len(days), 7):
        weeks.append(days[start:start + 7])
    return weeks
