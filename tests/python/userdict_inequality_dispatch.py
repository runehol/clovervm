from collections import UserDict


def unequal(other):
    return False


u = UserDict(a=1)
u.__eq__ = unequal
assert u == {'a': 1}
assert not u != {'a': 1}


class Truth:
    def __bool__(self):
        return True


class Override(UserDict):
    def __eq__(self, other):
        return Truth()


u = Override()
assert not u != {}


def not_implemented(self, other):
    return NotImplemented


Override.__eq__ = not_implemented
assert u.__ne__({}) is NotImplemented


def unequal_class(self, other):
    return False


Override.__eq__ = unequal_class
assert u != {}

failure = ValueError('truth failed')


class FailingTruth:
    def __bool__(self):
        raise failure


def failing_equal(self, other):
    return FailingTruth()


Override.__eq__ = failing_equal
try:
    u != {}
    assert False
except ValueError as error:
    assert error is failure
