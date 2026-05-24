# Python test set -- builtins module
#
# Surface selected from Python 3.14's built-in builtins module help and CPython
# bootstrap behavior. This file does not copy CPython test code.


class Marker:
    class_attr = 12


class CallableMarker:
    def __call__(self):
        return 37


obj = Marker()

assert isinstance(obj, Marker)
assert isinstance(True, bool)
assert isinstance(True, int)
assert isinstance(1, (str, int))
assert not isinstance("x", (list, tuple))
assert issubclass(Marker, object)
assert issubclass(bool, int)
assert issubclass(bool, (str, int))
assert not issubclass(str, (list, tuple))

setattr(obj, "dynamic_attr", 42)
assert getattr(obj, "dynamic_attr") == 42
assert hasattr(obj, "dynamic_attr")
assert getattr(obj, "missing_attr", 99) == 99
assert not hasattr(obj, "missing_attr")
delattr(obj, "dynamic_attr")
assert not hasattr(obj, "dynamic_attr")

assert vars(obj) is obj.__dict__
assert callable(Marker)
assert callable(CallableMarker())
assert not callable(obj)


def contains(seq, expected):
    for item in seq:
        if item == expected:
            return True
    return False


obj.name_from_instance = "present"
obj_names = dir(obj)
assert contains(obj_names, "name_from_instance")
assert contains(obj_names, "class_attr")
assert contains(obj_names, "__dict__")
assert contains(dir(), "obj_names")
