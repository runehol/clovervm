# Float comparisons cover float, int, bool, signed zero, infinity, and NaN values.
assert 1.0 < 2.0
assert not 2.0 < 1.0
assert 1.0 <= 1.0
assert not 2.0 <= 1.0
assert 2.0 > 1.0
assert not 1.0 > 2.0
assert 2.0 >= 2.0
assert not 1.0 >= 2.0
assert 1.0 == 1.0
assert not 1.0 == 2.0
assert 1.0 != 2.0
assert not 1.0 != 1.0

assert 1 < 2.0
assert not 2 < 1.0
assert 1 <= 1.0
assert not 2 <= 1.0
assert 2 > 1.0
assert not 1 > 2.0
assert 2 >= 2.0
assert not 1 >= 2.0
assert 1 == 1.0
assert not 1 == 2.0
assert 1 != 2.0
assert not 1 != 1.0

assert 1.0 < 2
assert not 2.0 < 1
assert 1.0 <= 1
assert not 2.0 <= 1
assert 2.0 > 1
assert not 1.0 > 2
assert 2.0 >= 2
assert not 1.0 >= 2
assert 1.0 == 1
assert not 1.0 == 2
assert 1.0 != 2
assert not 1.0 != 1
assert True == 1.0
assert False == 0.0
assert not True != 1.0
assert not False != 0.0
assert True < 2.0
assert False >= 0.0

assert 0.0 == -0.0
assert not 0.0 != -0.0
assert 0.0 <= -0.0
assert 0.0 >= -0.0

infinity = 1e300 * 1e300
nan = infinity / infinity
assert not nan < nan
assert not nan <= nan
assert not nan > nan
assert not nan >= nan
assert not nan == nan
assert nan != nan
