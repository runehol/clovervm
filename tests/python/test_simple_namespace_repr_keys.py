# Python test set -- types.SimpleNamespace string-subclass keys
#
# Regression expectations verified against CPython 3.14.5. These tests are
# original and do not copy CPython test code.

from types import SimpleNamespace


class KeyWithLength(str):
    def __len__(self):
        raise AssertionError("key.__len__ must not be called")


class KeyWithAddition(str):
    def __add__(self, other):
        raise AssertionError("key.__add__ must not be called")


class KeyWithStringConversion(str):
    def __str__(self):
        raise AssertionError("key.__str__ must not be called")


def namespace_with_key(key, value):
    namespace = SimpleNamespace()
    namespace.__dict__[key] = value
    return namespace


assert repr(namespace_with_key(KeyWithLength("length_key"), 1)) == "namespace(length_key=1)"
assert repr(namespace_with_key(KeyWithLength(""), 2)) == "namespace()"
assert repr(namespace_with_key(KeyWithAddition("addition_key"), 3)) == "namespace(addition_key=3)"
assert repr(namespace_with_key(KeyWithStringConversion("conversion_key"), 4)) == "namespace(conversion_key=4)"

recursive = namespace_with_key(KeyWithAddition("recursive_key"), None)
recursive.recursive_key = recursive
assert repr(recursive) == "namespace(recursive_key=namespace(...))"
