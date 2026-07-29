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

# Instance namespace views are fresh objects backed by the same live storage.
first_vars = vars(obj)
second_vars = obj.__dict__
assert first_vars is not second_vars
assert first_vars.__class__.__name__ == "slotdict"
first_vars["vars_probe"] = 41
assert second_vars["vars_probe"] == 41
obj.vars_probe = 42
assert first_vars["vars_probe"] == 42


# Function and module __dict__ attributes expose slotdict namespace views.
def function_dict_probe():
    pass


assert function_dict_probe.__dict__.__class__.__name__ == "slotdict"
assert __builtins__.__dict__.__class__.__name__ == "slotdict"

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

no_arg_dir_is_unimplemented = False
try:
    dir()
except UnimplementedError:
    no_arg_dir_is_unimplemented = True
assert no_arg_dir_is_unimplemented

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
assert contains(builtin_names, "SyntaxError")
assert contains(builtin_names, "IndentationError")
assert contains(builtin_names, "SystemError")
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

# repr and len dispatch to builtin and user-visible special methods.
assert repr(42) == "42"
assert repr(1.5) == "1.5"
assert repr(1.0) == "1.0"
assert repr(1e20) == "1e+20"

assert len(()) == 0
assert len((1, 2, 3)) == 3
assert len([]) == 0
assert len([1, 2]) == 2
assert len({}) == 0
assert len({"a": 1, "b": 2}) == 2
assert len("abc") == 3

# hash dispatches to __hash__ and canonicalizes reserved and oversized results.
assert hash(42) == 42
assert hash(-1) == -2
assert hash(True) == 1
assert hash(False) == 0


class CustomHash:
    def __hash__(self):
        return 123


class MinusOneHash:
    def __hash__(self):
        return -1


class LargeHash:
    def __hash__(self):
        return 288230376151711744


assert hash(CustomHash()) == 123
assert hash(MinusOneHash()) == -2
assert hash(LargeHash()) == 1
assert hash(10**100) == 69889855055785222
assert hash(10**101) == 122437798254428734

# sum, any, all, min, and max consume iterables with their builtin semantics.
assert sum(()) == 0
assert sum((1, 2, 3)) == 6
assert sum([1, 2, 3], 10) == 16

assert not any(())
assert not any((0, False, None))
assert any((0, 4, False))

assert all(())
assert all((1, True, 2))
assert not all((1, 0, True))

assert min((3, 1, 2)) == 1
assert min(3, 1, 2) == 1
assert max([3, 1, 2]) == 3
assert max(3, 1, 2) == 3
