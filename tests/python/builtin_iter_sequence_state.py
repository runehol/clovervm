# Behavioral cases checked against CPython 3.14.5; see PyObject_GetIter in
# Objects/abstract.c, slot_tp_iter in Objects/typeobject.c, and
# iter_iternext in Objects/iterobject.c. These are independent regressions.
def raises(kind, function, *args):
    try:
        function(*args)
    except kind:
        return
    assert False


class Sequence:
    def __init__(self):
        self.items = [10, 20]
        self.calls = []

    def __getitem__(self, index):
        self.calls.append(index)
        return self.items[index]


class Recovering(Sequence):
    def __init__(self):
        Sequence.__init__(self)
        self.failed = False

    def __getitem__(self, index):
        if not self.failed:
            self.failed = True
            raise ValueError('retry')
        return Sequence.__getitem__(self, index)


source = Recovering()
iterator = iter(source)
raises(ValueError, next, iterator)
assert next(iterator) == 10
assert source.calls == [0]


class Ends(IndexError):
    pass


class StopSource:
    def __init__(self):
        self.calls = 0

    def __getitem__(self, index):
        self.calls += 1
        raise StopIteration('discard payload')


source = StopSource()
iterator = iter(source)
try:
    next(iterator)
except StopIteration as error:
    assert error.value is None
else:
    assert False
assert next(iterator, 'default') == 'default'
assert source.calls == 1


class SubclassEnd:
    def __getitem__(self, index):
        raise Ends


assert list(SubclassEnd()) == []


class Changing:
    def __getitem__(self, index):
        return index


def acquire(obj):
    return iter(obj)


def read(obj):
    return next(obj)


source = Changing()
iterator = acquire(source)
assert read(iterator) == 0
def changed_item(self, index):
    return index + 100


Changing.__getitem__ = changed_item
assert read(iterator) == 101
del Changing.__getitem__
raises(TypeError, read, iterator)
def restored_item(self, index):
    return index + 200


Changing.__getitem__ = restored_item
assert read(iterator) == 202
def changed_iter(self):
    return iter([7])


Changing.__iter__ = changed_iter
assert list(acquire(source)) == [7]
Changing.__iter__ = None
raises(TypeError, acquire, source)
del Changing.__iter__
assert read(acquire(source)) == 200


# Reentry increments current state after callbacks; it must not restore index+1.
class Reentrant:
    def __init__(self):
        self.inside = False
        self.calls = []

    def __getitem__(self, index):
        self.calls.append(index)
        if not self.inside:
            self.inside = True
            assert next(reentrant_iterator) == index
            self.inside = False
        return index


source = Reentrant()
reentrant_iterator = iter(source)
assert next(reentrant_iterator) == 0
assert next(reentrant_iterator) == 2
assert source.calls == [0, 0, 2, 2]


# An exception from __iter__ never triggers the sequence fallback.
class BrokenIter(Sequence):
    def __iter__(self):
        raise AttributeError('iteration failed')


source = BrokenIter()
raises(AttributeError, iter, source)
assert source.calls == []


# Reentrant exhaustion cannot be undone by the outer successful callback.
class ReentrantEnd:
    def __init__(self):
        self.inside = False
        self.calls = 0

    def __getitem__(self, index):
        self.calls += 1
        if self.inside:
            raise IndexError
        self.inside = True
        raises(StopIteration, next, ending_iterator)
        return 11


source = ReentrantEnd()
ending_iterator = iter(source)
assert next(ending_iterator) == 11
raises(StopIteration, next, ending_iterator)
assert source.calls == 2


# Both direct dunders reject unrelated receivers without touching state.
iterator = iter(Sequence())
iterator_type = iterator.__class__
raises(TypeError, iterator_type.__iter__, 1)
raises(TypeError, iterator_type.__next__, 1)
assert next(iterator) == 10
