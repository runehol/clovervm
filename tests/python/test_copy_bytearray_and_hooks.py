# Independently written regression tests, checked against CPython 3.14.10.
import copy


empty = bytearray()
duplicate = copy.copy(empty)
assert duplicate.__class__ is bytearray
assert duplicate is not empty
assert len(duplicate) == 0

original = bytearray(b"abc")
duplicate = copy.copy(original)
assert duplicate.__class__ is bytearray
assert duplicate is not original
assert bytes(duplicate) == b"abc"
duplicate[0] = 81
assert bytes(original) == b"abc"
original.append(120)
assert bytes(original) == b"abcx"
assert bytes(duplicate) == b"Qbc"


class StaticHooks:
    def hook(target, **changes):
        return (target, changes)

    __copy__ = staticmethod(hook)
    __replace__ = staticmethod(hook)


class ClassHooks:
    def hook(cls, target, **changes):
        return (target, changes, cls)

    __copy__ = classmethod(hook)
    __replace__ = classmethod(hook)


class InheritedHooks(ClassHooks):
    pass


class CallableHook:
    def __call__(self, target, **changes):
        return (target, changes, self)


callable_hook = CallableHook()


class CallableHooks:
    __copy__ = callable_hook
    __replace__ = callable_hook


class HookDescriptor:
    def __get__(self, instance, owner):
        assert instance is None
        assert owner is DescriptorHooks
        return callable_hook


class DescriptorHooks:
    __copy__ = HookDescriptor()
    __replace__ = HookDescriptor()


field = []
for case in ((StaticHooks, None), (InheritedHooks, InheritedHooks),
             (CallableHooks, callable_hook), (DescriptorHooks, callable_hook)):
    obj = case[0]()
    copied = copy.copy(obj)
    assert copied[0] is obj
    assert len(copied[1]) == 0
    replaced = copy.replace(obj, obj=field)
    assert replaced[0] is obj
    assert len(replaced[1]) == 1
    assert replaced[1]["obj"] is field
    if case[1] is not None:
        assert copied[2] is case[1]
        assert replaced[2] is case[1]


descriptor_error = ValueError("descriptor lookup")


class RaisingDescriptor:
    def __get__(self, instance, owner):
        raise descriptor_error


class RaisingHooks:
    __copy__ = RaisingDescriptor()
    __replace__ = RaisingDescriptor()


for operation in (copy.copy, copy.replace):
    try:
        operation(RaisingHooks())
        assert False
    except ValueError as caught:
        assert caught is descriptor_error
