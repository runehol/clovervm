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


class Inherited(Sequence):
    pass


source = Inherited()
iterator = iter(source)
assert source.calls == []
assert iter(iterator) is iterator
assert next(iterator) == 10
source.items.append(30)
assert next(iterator) == 20
assert next(iterator) == 30
raises(StopIteration, next, iterator)
source.items.append(40)
raises(StopIteration, next, iterator)
assert source.calls == [0, 1, 2, 3]
assert list(Sequence()) == [10, 20]
assert tuple(Sequence()) == (10, 20)
items = []
for item in Sequence():
    items.append(item)
assert items == [10, 20]


class Disabled(Sequence):
    __iter__ = None


source = Disabled()
raises(TypeError, iter, source)
raises(TypeError, list, source)
assert source.calls == []


class Plain:
    pass


source = Plain()
def instance_iter():
    return iter([1])


def instance_item(index):
    return index


source.__iter__ = instance_iter
source.__getitem__ = instance_item
raises(TypeError, iter, source)


class Producer:
    def __init__(self, result):
        self.result = result
        self.calls = 0

    def __iter__(self):
        self.calls += 1
        return self.result


result = Plain()
def instance_next():
    return 1


result.__next__ = instance_next
producer = Producer(result)
raises(TypeError, iter, producer)
assert producer.calls == 1
raises(TypeError, list, Producer(result))


class IteratorBase:
    def __next__(self):
        raise AssertionError('validation must not advance')


class IteratorChild(IteratorBase):
    pass


result = IteratorChild()
assert iter(Producer(result)) is result


class NoncallableNext:
    __next__ = None


result = NoncallableNext()
assert iter(Producer(result)) is result
raises(TypeError, next, result)
NoncallableNext.__next__ = 42
assert iter(Producer(result)) is result
raises(TypeError, next, result)
del NoncallableNext.__next__
raises(TypeError, iter, Producer(result))
NoncallableNext.__next__ = IteratorBase.__next__
assert iter(Producer(result)) is result


class NextDescriptor:
    def __get__(self, obj, owner):
        raise AssertionError('validation must not bind')


class DescriptorNext:
    __next__ = NextDescriptor()


result = DescriptorNext()
assert iter(Producer(result)) is result


class NoItemCall:
    __getitem__ = None


iterator = iter(NoItemCall())
raises(TypeError, next, iterator)
