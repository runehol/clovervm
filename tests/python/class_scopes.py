class DefaultSource:
    x = 10

    def method(self, a=x + 1):
        return a

assert DefaultSource().method() == 11


# A class definition binds a class object whose calls allocate instances of that class.
class BoundClass:
    pass


assert BoundClass.__name__ == "BoundClass"
bound_instance = BoundClass()
assert bound_instance.__class__ is BoundClass


# Class __dict__ exposes a live, writable view of its namespace.
class MutableClassNamespace:
    x = 3


assert MutableClassNamespace.__dict__["x"] == 3
MutableClassNamespace.__dict__["y"] = 9
assert MutableClassNamespace.y == 9


# Assigning __class__ changes an instance to another compatible receiver shape.
class OriginalAssignedClass:
    pass


class ReplacementAssignedClass:
    marker = 42


reassigned_instance = OriginalAssignedClass()
reassigned_instance.__class__ = ReplacementAssignedClass
assert reassigned_instance.__class__ is ReplacementAssignedClass
assert reassigned_instance.marker == 42


class InitialClassSlot:
    pass

initial_obj = InitialClassSlot()
assert initial_obj.__class__ is InitialClassSlot

class ReadsEarlierBinding:
    x = 1
    y = x + 2

assert ReadsEarlierBinding.y == 3

def outer(seed):
    a = seed + 1
    b = seed + 2
    c = (a + b) * (seed + 3)

    class Inner:
        x = 11
        y = x + 13

    d = (a + b) * (c + Inner.y)
    return d + a + b + c

assert outer(4) == 1199
