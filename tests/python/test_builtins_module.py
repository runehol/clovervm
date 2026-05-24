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

builtin_names = dir(__builtins__)
assert contains(builtin_names, "Ellipsis")
assert contains(builtin_names, "NotImplemented")
assert contains(builtin_names, "object")
assert contains(builtin_names, "type")
assert contains(builtin_names, "str")
assert contains(builtin_names, "int")
assert contains(builtin_names, "bool")
assert contains(builtin_names, "float")
assert contains(builtin_names, "list")
assert contains(builtin_names, "tuple")
assert contains(builtin_names, "dict")
assert contains(builtin_names, "BaseException")
assert contains(builtin_names, "Exception")
assert contains(builtin_names, "TypeError")
assert not contains(builtin_names, "NoneType")
assert not contains(builtin_names, "NotImplementedType")
assert not contains(builtin_names, "ellipsis")
assert not contains(builtin_names, "module")
assert not contains(builtin_names, "module_loader")
assert not contains(builtin_names, "module_spec")
assert not contains(builtin_names, "range_iterator")
assert not contains(builtin_names, "tuple_iterator")
assert not contains(builtin_names, "list_iterator")
assert not contains(builtin_names, "dict_keys")
assert not contains(builtin_names, "dict_values")
assert not contains(builtin_names, "dict_items")
assert not contains(builtin_names, "dict_keyiterator")
assert not contains(builtin_names, "dict_valueiterator")
assert not contains(builtin_names, "dict_itemiterator")
