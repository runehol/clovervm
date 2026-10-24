# Python test set -- copy module
# Behavior checked against CPython 3.14.5; independently written tests.
import copy

for value in (None, False, True, 17, 2**62, 1.25, "abc", b"abc", (),
              ([],), slice(1, 3), Ellipsis, NotImplemented):
    assert copy.copy(value) is value

child = []
for original in ([], [child], {}, {"child": child}):
    result = copy.copy(original)
    assert result is not original
    # Dict value equality is not implemented by the VM; inspect contents below.
    assert len(result) == len(original)
items = [child]
items_copy = copy.copy(items)
assert items_copy[0] is child
items_copy.append(3)
assert len(items) == 1
mapping = {"child": child}
mapping_copy = copy.copy(mapping)
assert mapping_copy["child"] is child
mapping_copy["extra"] = 4
assert "extra" not in mapping
child.append(9)
assert items_copy[0][0] == 9
assert mapping_copy["child"][0] == 9


class Hook:
    def __copy__(self):
        return self.payload

    def __replace__(self, **changes):
        return changes


class Derived(Hook):
    pass


assert copy.copy(Hook) is Hook
assert copy.copy(Derived) is Derived
obj = Derived()
obj.payload = child
# Instance hooks must be ignored, and inherited class hooks must work.
obj.__copy__ = 42
obj.__replace__ = 42
assert copy.copy(obj) is child
assert len(copy.replace(obj)) == 0
changes = {"obj": child, "unknown": 4}
result = copy.replace(obj, **changes)
assert result["obj"] is child
assert result["unknown"] == 4
assert copy.copy(x=obj) is child


class ListHook(list):
    def __copy__(self):
        return 73


assert copy.copy(ListHook()) == 73


class Missing:
    pass


class NullHook:
    __copy__ = None
    __replace__ = None


for value in (Missing(), NullHook(), ListHook):
    # Classes are copyable, but lack replacement hooks.
    try:
        copy.replace(value)
        assert False
    except TypeError:
        pass
for value in (Missing(), NullHook()):
    try:
        copy.copy(value)
        assert False
    except copy.Error:
        pass
assert copy.error is copy.Error


class Noncallable:
    __copy__ = 42
    __replace__ = 42


for operation in (copy.copy, copy.replace):
    try:
        operation(Noncallable())
        assert False
    except TypeError:
        pass


class Broken:
    def __copy__(self):
        raise ValueError("copy hook")

    def __replace__(self, **changes):
        raise ValueError("replace hook")


for operation in (copy.copy, copy.replace):
    try:
        operation(Broken())
        assert False
    except ValueError:
        pass
for value in (None, 1, [], {}, (1,)):
    try:
        copy.replace(value)
        assert False
    except TypeError:
        pass
for operation in (copy.copy, copy.replace):
    try:
        operation()
        assert False
    except TypeError:
        pass
    try:
        operation(obj, obj)
        assert False
    except TypeError:
        pass
try:
    copy.copy(obj, unknown=1)
    assert False
except TypeError:
    pass
try:
    copy.replace(obj=obj)
    assert False
except TypeError:
    pass


class Record:
    def __init__(self, value):
        self.value = value

    def __replace__(self, **changes):
        return Record(changes.get("value", self.value))


record = Record(child)
replaced = copy.replace(record)
assert replaced is not record
assert replaced.value is child
assert copy.replace(record, value=7).value == 7
assert record.value is child


class NoListHook(list):
    pass


class NoDictHook(dict):
    pass


class NoIntHook(int):
    pass


# Subclasses must not take the exact-builtin copy paths.
for value in (NoListHook(), NoDictHook(), NoIntHook(3)):
    try:
        copy.copy(value)
        assert False
    except copy.Error:
        pass


class MutableHook:
    def __copy__(self):
        return 1

    def __replace__(self, **changes):
        return 2


def new_copy(self):
    return 3


def new_replace(self, **changes):
    return 4


mutable = MutableHook()
assert copy.copy(mutable) == 1
assert copy.replace(mutable) == 2
MutableHook.__copy__ = new_copy
MutableHook.__replace__ = new_replace
assert copy.copy(mutable) == 3
assert copy.replace(mutable) == 4

# Self references are preserved rather than recursively copied.
cycle = []
cycle.append(cycle)
cycle_copy = copy.copy(cycle)
assert cycle_copy is not cycle
assert cycle_copy[0] is cycle
cycle_dict = {}
cycle_dict["self"] = cycle_dict
assert copy.copy(cycle_dict)["self"] is cycle_dict


class ReductionOnly:
    def __reduce_ex__(self, protocol):
        raise ValueError("reduction must not run")

    def __reduce__(self):
        raise ValueError("reduction must not run")


try:
    copy.copy(ReductionOnly())
    assert False
except copy.Error:
    pass

# The narrow native primitive returns real class objects, including for SMIs.
from builtins import __clover_runtime_type__
assert __clover_runtime_type__(3) is int
assert __clover_runtime_type__(2**62) is int
assert __clover_runtime_type__(True) is bool
assert __clover_runtime_type__(obj) is Derived
assert __clover_runtime_type__(NoListHook()) is NoListHook
assert __clover_runtime_type__(Derived) is type


class ArbitraryResult:
    def __copy__(self):
        return None

    def __replace__(self, **changes):
        return NotImplemented


assert copy.copy(ArbitraryResult()) is None
assert copy.replace(ArbitraryResult()) is NotImplemented


class DictHook(dict):
    def __copy__(self):
        return 81


assert copy.copy(DictHook()) == 81

hook_error = ValueError("preserve exception identity")


class RaisesSame:
    def __copy__(self):
        raise hook_error

    def __replace__(self, **changes):
        raise hook_error


for operation in (copy.copy, copy.replace):
    try:
        operation(RaisesSame())
        assert False
    except ValueError as caught:
        assert caught is hook_error


class AttributeFailure:
    def __copy__(self):
        raise AttributeError("inside copy hook")

    def __replace__(self, **changes):
        raise AttributeError("inside replace hook")


for operation in (copy.copy, copy.replace):
    try:
        operation(AttributeFailure())
        assert False
    except AttributeError:
        pass
