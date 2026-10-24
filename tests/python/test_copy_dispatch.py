# Independently written regression tests, checked against CPython 3.14.5.
import copy


class First:
    def __copy__(self):
        return 1

    def __replace__(self, **changes):
        return 2


class Second:
    def __copy__(self):
        return 3

    def __replace__(self, **changes):
        return 4


value = First()
# Reuse the same call sites across changes to the object's real class.
for expected in (1, 3, 3):
    assert copy.copy(value) == expected
    assert copy.replace(value) == expected + 1
    value.__class__ = Second


class Parent:
    def __copy__(self):
        return 5

    def __replace__(self, **changes):
        return 6


class Child(Parent):
    pass


def changed_copy(self):
    return 7


def changed_replace(self, **changes):
    return 8


value = Child()
for expected in (5, 7):
    assert copy.copy(value) == expected
    assert copy.replace(value) == expected + 1
    Parent.__copy__ = changed_copy
    Parent.__replace__ = changed_replace

Child.__copy__ = First.__copy__
Child.__replace__ = First.__replace__
assert copy.copy(value) == 1
assert copy.replace(value) == 2
del Child.__copy__
del Child.__replace__
assert copy.copy(value) == 7
assert copy.replace(value) == 8

# General-key dictionaries exercise a different dict representation.
shared = []
key = 2**62
original = {key: shared, 3: shared}
duplicate = copy.copy(original)
assert duplicate is not original
assert len(duplicate) == 2
assert duplicate[key] is shared
assert duplicate[3] is shared
original.clear()
assert len(duplicate) == 2
assert duplicate[key] is shared
last = duplicate.popitem()
assert last[0] == 3
assert last[1] is shared
first = duplicate.popitem()
assert first[0] is key
assert first[1] is shared

hook_error = TypeError("from hook")


class SometimesRaises:
    def __copy__(self):
        if self.fail:
            raise hook_error
        return self

    def __replace__(self, **changes):
        if self.fail:
            raise hook_error
        return changes


value = SometimesRaises()
value.fail = True
for operation in (copy.copy, copy.replace):
    try:
        operation(value)
        assert False
    except TypeError as caught:
        assert caught is hook_error
value.fail = False
assert copy.copy(value) is value
changes = copy.replace(value, obj=shared, cls=shared, func=shared)
assert len(changes) == 3
assert changes["obj"] is shared
assert changes["cls"] is shared
assert changes["func"] is shared
