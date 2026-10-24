# Checked against CPython: both methods count bits in the absolute magnitude.
class I(int):
    def __abs__(self):
        raise AssertionError

    def __int__(self):
        raise AssertionError

    def __index__(self):
        raise AssertionError


class J(I):
    pass


cases = [(0, 0, 0), (13, 4, 3), (-13, 4, 3)]
for bits in (1, 2, 31, 32, 33, 56, 57, 58, 59, 63, 64, 65, 1000):
    power = 2**bits
    cases.append((power, bits + 1, 1))
    cases.append((-power, bits + 1, 1))
    cases.append((power - 1, bits, bits))
    cases.append((-(power - 1), bits, bits))
    cases.append((power + 1, bits + 1, 2))
    cases.append((-(power + 1), bits + 1, 2))
cases.append((2**1000 + 2**64 + 2**32 + 1, 1001, 4))
cases.append((-(2**1000 + 2**64 + 2**32 + 1), 1001, 4))
for case in cases:
    for cls in (int, I, J):
        integer = cls(case[0])
        assert integer.bit_length() == case[1]
        assert integer.bit_count() == case[2]
        assert int.bit_length(integer) == case[1]
        assert int.bit_count(integer) == case[2]
        assert integer.bit_length().__class__ is int
        assert integer.bit_count().__class__ is int

assert False.bit_length() == 0
assert False.bit_count() == 0
assert True.bit_length() == 1
assert True.bit_count() == 1
assert True.bit_count().__class__ is int


class Override(int):
    def bit_length(self):
        return "length override"

    def bit_count(self):
        return "count override"


assert Override(13).bit_length() == "length override"
assert Override(13).bit_count() == "count override"
assert int.bit_length(Override(13)) == 4
assert int.bit_count(Override(13)) == 3

for method in (int.bit_length, int.bit_count):
    for value in (None, 1.0, "1", b"1", [], {}):
        raised = False
        try:
            method(value)
        except TypeError:
            raised = True
        assert raised
    raised = False
    try:
        method()
    except TypeError:
        raised = True
    assert raised
    raised = False
    try:
        method(1, 2)
    except TypeError:
        raised = True
    assert raised
