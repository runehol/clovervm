# Python test set -- types module
#
# Surface selected from Python 3.14's Lib/types.py and module help. This file
# does not copy CPython test code.

import sys
import types


def sample_function():
    return 37


assert types.FunctionType is sample_function.__class__
assert types.LambdaType is types.FunctionType
assert types.BuiltinFunctionType is types.FunctionType
assert types.BuiltinMethodType is types.FunctionType
assert types.MethodType is types.FunctionType
assert isinstance(sample_function, types.FunctionType)
assert isinstance(sample_function.__code__, types.CodeType)
assert sample_function.__code__.__class__ is types.CodeType
try:
    sample_function.__code__ = sample_function.__code__
    assert False
except AttributeError:
    pass

assert types.ModuleType is sys.__class__
assert isinstance(sys, types.ModuleType)

assert None.__class__ is types.NoneType
assert isinstance(None, types.NoneType)

namespace = types.SimpleNamespace()
namespace.answer = 42
assert namespace.answer == 42
assert isinstance(namespace, types.SimpleNamespace)
assert repr(namespace) == "namespace(...)"


class Base:
    pass


class Derived(Base):
    pass


assert types.get_original_bases(Derived)[0] is Base
Derived.__orig_bases__ = (types.SimpleNamespace,)
assert types.get_original_bases(Derived)[0] is types.SimpleNamespace

assert types.__all__[2] == "CodeType"
