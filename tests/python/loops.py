total = 0
for x in range(5):
    total += x
assert total == 10

total = 0
for x in range(1, 4):
    total += x
assert total == 6

total = 0
for x in range(1, 8, 3):
    total += x
assert total == 12

total = 0
for x in range(5, -1, -2):
    total += x
assert total == 9

total = 0
for x in range(3):
    for y in range(2):
        total += x + y
assert total == 9

def sum_range(n):
    total = 0
    for x in range(n):
        total += x
    return total

assert sum_range(5) == 10

def sum_pairs(n):
    total = 0
    for x in range(n):
        for y in range(2):
            total += x + y
    return total

assert sum_pairs(3) == 9

real_range = range

def range(n):
    return real_range(1, n)

total = 0
for x in range(4):
    total += x
assert total == 6


# for loops drive user-defined, tuple, and list iterators through exhaustion.
class Counter:
    def __init__(self):
        self.i = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.i == 3:
            raise StopIteration
        value = self.i
        self.i += 1
        return value


total = 0
for x in Counter():
    total += x
assert total == 3

total = 0
for x in (1, 2, 3):
    total += x
assert total == 6

total = 0
for x in [1, 2, 3]:
    total += x
assert total == 6

# range, iter, and next produce and exhaust builtin iterators correctly.
range_one_arg = real_range(5)
assert next(range_one_arg) == 0
assert next(range_one_arg) == 1

range_two_args = real_range(2, 5)
assert next(range_two_args) == 2
assert next(range_two_args) == 3

range_three_args = real_range(2, 9, 3)
assert next(range_three_args) == 2
assert next(range_three_args) == 5
assert next(range_three_args) == 8

range_from_iter = iter(real_range(3))
assert next(range_from_iter) == 0
assert next(range_from_iter) == 1

tuple_iterator = iter((4, 5))
assert next(tuple_iterator) == 4
assert next(tuple_iterator) == 5
try:
    next(tuple_iterator)
    tuple_iterator_exhausted = False
except StopIteration:
    tuple_iterator_exhausted = True
assert tuple_iterator_exhausted
assert next(tuple_iterator, 42) == 42

list_iterator = iter([4, 5])
assert next(list_iterator) == 4
assert next(list_iterator) == 5
try:
    next(list_iterator)
    list_iterator_exhausted = False
except StopIteration:
    list_iterator_exhausted = True
assert list_iterator_exhausted


# Cached iterator calls observe method replacement and preserve varargs adaptation.
class ReplaceableIterator:
    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration


def first_or_zero(iterator):
    for value in iterator:
        return value
    return 0


replaceable_iterator = ReplaceableIterator()
assert first_or_zero(replaceable_iterator) == 0


def replacement_next(self):
    return 7


ReplaceableIterator.__next__ = replacement_next
assert first_or_zero(replaceable_iterator) == 7


class VarargsIterator:
    def __init__(self):
        self.done = False

    def __iter__(*args):
        self = args[0]
        self.done = False
        return self

    def __next__(*args):
        self = args[0]
        if self.done:
            raise StopIteration
        self.done = True
        return 3


def first(iterator):
    for value in iterator:
        return value


varargs_iterator = VarargsIterator()
assert first(varargs_iterator) == 3
assert first(varargs_iterator) == 3
