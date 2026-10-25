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


class Plain:
    pass


# Generic loop semantics cannot depend on the public builtins.iter binding.
import builtins
original_iter = builtins.iter
def replacement_iter(source):
    return None


builtins.iter = replacement_iter
items = []
for item in Sequence():
    items.append(item)
builtins.iter = original_iter
assert items == [10, 20]


class NoncallableIter(Sequence):
    __iter__ = 1


source = NoncallableIter()
raises(TypeError, iter, source)
assert source.calls == []


# Warmed generic loop acquisition and advancement must honor MRO changes.
class Empty(Sequence):
    def __getitem__(self, index):
        raise IndexError


class LoopChild(Empty):
    pass


def collect(source):
    result = []
    for item in source:
        result.append(item)
    return result


assert collect(LoopChild()) == []
def inherited_iter(self):
    return iter([9])


Empty.__iter__ = inherited_iter
assert collect(LoopChild()) == [9]
Empty.__iter__ = None
raises(TypeError, collect, LoopChild())
del Empty.__iter__
assert collect(LoopChild()) == []


# An invalid iterator is rejected before any loop body executes.
class Invalid:
    def __iter__(self):
        return Plain()


events = []
try:
    for item in Invalid():
        events.append(item)
except TypeError:
    pass
else:
    assert False
assert events == []


class IterStops(Sequence):
    def __iter__(self):
        raise StopIteration('construction')


raises(StopIteration, iter, IterStops())
raises(StopIteration, collect, IterStops())


def wrong_keyword():
    return iter(obj=Sequence())


raises(TypeError, wrong_keyword)
raises(TypeError, iter)
raises(TypeError, iter, Sequence(), None)


# IndexError terminates only the legacy sequence adapter, not an ordinary iterator.
class NextIndexError:
    def __iter__(self):
        return self

    def __next__(self):
        raise IndexError('ordinary iterator failure')


raises(IndexError, list, NextIndexError())
