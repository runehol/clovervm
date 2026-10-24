# Python test set -- calendar module
#
# Reference checksums generated using CPython 3.14.5 calendar public APIs.
# For each year: monthrange followed by every valid weekday (base 7),
# and every month grid for all seven week starts (base 37), modulo 1000000007.
# Monthrange entries are folded as checksum * 37 + weekday * 32 + length.
# Fixtures cover full years, including zero/negative years and bigints.

import calendar


CASES = [
    (-10000000000000000000000000000000000000000, 157437544, 176699643),
    (-801, 136022126, 404445570),
    (-748, 707237829, 224328121),
    (-695, 194251830, 4926021),
    (-642, 957434021, 515421542),
    (-589, 551530712, 416044435),
    (-536, 895380069, 727547039),
    (-483, 827977778, 688136658),
    (-430, 194251830, 4926021),
    (-400, 157437544, 176699643),
    (-377, 957434021, 515421542),
    (-324, 626254101, 9713643),
    (-271, 833524116, 981012982),
    (-218, 551530712, 416044435),
    (-165, 194251830, 4926021),
    (-112, 706330592, 847083301),
    (-100, 827977778, 688136658),
    (-59, 249136924, 410736679),
    (-6, 833524116, 981012982),
    (-4, 707237829, 224328121),
    (-1, 136022126, 404445570),
    (0, 157437544, 176699643),
    (1, 827977778, 688136658),
    (4, 249031759, 744610543),
    (47, 551530712, 416044435),
    (100, 136022126, 404445570),
    (153, 827977778, 688136658),
    (206, 249136924, 410736679),
    (259, 833524116, 981012982),
    (312, 707237829, 224328121),
    (365, 136022126, 404445570),
    (400, 157437544, 176699643),
    (418, 827977778, 688136658),
    (471, 194251830, 4926021),
    (524, 157437544, 176699643),
    (577, 249136924, 410736679),
    (630, 136022126, 404445570),
    (683, 827977778, 688136658),
    (736, 626254101, 9713643),
    (789, 957434021, 515421542),
    (1600, 157437544, 176699643),
    (1700, 136022126, 404445570),
    (1800, 249136924, 410736679),
    (1900, 827977778, 688136658),
    (1970, 194251830, 4926021),
    (1999, 136022126, 404445570),
    (2000, 157437544, 176699643),
    (2004, 249031759, 744610543),
    (2100, 136022126, 404445570),
    (2400, 157437544, 176699643),
    (9999, 136022126, 404445570),
    (10000, 157437544, 176699643),
    (10000000000000000000000000000000000000000, 157437544, 176699643),
]


def check_year(case):
    year = case[0]
    dates = 0
    grids = 0
    for month in range(1, 13):
        bounds = calendar.monthrange(year, month)
        dates = (dates * 37 + bounds[0] * 32 + bounds[1]) % 1000000007
        for day in range(1, bounds[1] + 1):
            value = calendar.weekday(year, month, day)
            dates = (dates * 7 + value) % 1000000007
            assert calendar.weekday(year + 400, month, day) == value
        for start in range(7):
            calendar.setfirstweekday(start)
            grid = calendar.monthcalendar(year, month)
            assert 4 <= len(grid) <= 6
            next_day = 1
            for week in grid:
                assert len(week) == 7
                for day in week:
                    grids = (grids * 37 + day) % 1000000007
                    if day != 0:
                        assert day == next_day
                        next_day += 1
            assert next_day == bounds[1] + 1
    assert dates == case[1], case
    assert grids == case[2], case


for case in CASES:
    check_year(case)

# Broad half-open ranges, with an independent direct leap-year count.
for begin in range(-801, 802, 53):
    for width in (0, 1, 4, 100, 400, 801):
        count = 0
        for year in range(begin, begin + width):
            if year % 4 == 0 and (year % 100 != 0 or year % 400 == 0):
                count += 1
        assert calendar.leapdays(begin, begin + width) == count
        assert calendar.leapdays(begin + width, begin) == -count
calendar.setfirstweekday(0)
