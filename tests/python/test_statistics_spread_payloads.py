import statistics as s


class IntSubclass(int):
    def __int__(self):
        raise AssertionError("custom int conversion")

    def __sub__(self, other):
        raise AssertionError("custom subtraction")

    def as_integer_ratio(self):
        raise AssertionError("custom ratio")


class FloatSubclass(float):
    def __float__(self):
        raise AssertionError("custom float conversion")

    def __sub__(self, other):
        raise AssertionError("custom subtraction")

    def as_integer_ratio(self):
        raise AssertionError("custom ratio")


assert s.variance([IntSubclass(2), IntSubclass(4)]).__class__ is int
assert s.variance([IntSubclass(2), IntSubclass(4)], IntSubclass(0)) == 20
assert s.pvariance([FloatSubclass(2), FloatSubclass(4)]).__class__ is float
assert s.pvariance([FloatSubclass(2), FloatSubclass(4)], FloatSubclass(2)) == 2.0
assert s.pstdev([FloatSubclass(1), IntSubclass(3)]) == 1.0
assert s.pvariance([IntSubclass(10**500), 10**500 + 2]) == 1


class Ephemeral:
    def __init__(self):
        self.remaining = 32

    def __iter__(self):
        return self

    def __next__(self):
        if self.remaining == 0:
            raise StopIteration
        self.remaining -= 1
        return FloatSubclass(5e-324)


assert s.pstdev(Ephemeral()) == 0.0
assert s.stdev(Ephemeral(), 0.0) == 0.0
