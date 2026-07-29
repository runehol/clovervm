class InitForwardsArguments:
    def __init__(self, x, y):
        self.value = x * 10 + y

assert InitForwardsArguments(4, 7).value == 47

class InitDefaults:
    def __init__(self, x, y=3):
        self.value = x * 10 + y

assert InitDefaults(4).value == 43

class SelfDefault:
    def __init__(self=1):
        pass

assert SelfDefault().__class__ is SelfDefault

class RemapDefaults:
    def __init__(self=1, x=2):
        self.value = x

assert RemapDefaults().value * 10 + RemapDefaults(5).value == 25

class InitVarargs:
    def __init__(self, x, *rest):
        self.value = x + rest[0] + rest[1]

assert InitVarargs(1, 20, 300).value == 321

class RebuildInit:
    def __init__(self):
        self.value = 1

def replacement(self):
    self.value = 2

first = RebuildInit()
RebuildInit.__init__ = replacement
second = RebuildInit()
assert first.value * 10 + second.value == 12

class InheritedInitBase:
    def __init__(self):
        self.value = 1

class InheritedInitDerived(InheritedInitBase):
    pass

assert InheritedInitDerived().value == 1

def inherited_init_replacement(self):
    self.value = 2

InheritedInitBase.__init__ = inherited_init_replacement
assert InheritedInitDerived().value == 2

class AddedInitBase:
    pass

class AddedInitDerived(AddedInitBase):
    pass

AddedInitDerived()

def added_init(self, value):
    self.value = value

AddedInitBase.__init__ = added_init
assert AddedInitDerived(3).value == 3
del AddedInitBase.__init__
assert AddedInitDerived().__class__ is AddedInitDerived

class InheritedNewBase:
    def __new__(cls):
        return 1

class InheritedNewDerived(InheritedNewBase):
    pass

assert InheritedNewDerived() == 1

def inherited_new_replacement(cls):
    return 2

InheritedNewBase.__new__ = inherited_new_replacement
assert InheritedNewDerived() == 2

class AddedNewBase:
    pass

class AddedNewDerived(AddedNewBase):
    pass

AddedNewDerived()

def added_new(cls, value):
    return value

AddedNewBase.__new__ = added_new
assert AddedNewDerived(4) == 4
del AddedNewBase.__new__
assert AddedNewDerived().__class__ is AddedNewDerived


# Class construction preserves keyword, default, varargs, and positional-only binding.
class KeywordInit:
    def __init__(self, a, b=2):
        self.value = a + b


assert KeywordInit(a=3).value == 5


class KeywordOnlyInitDefault:
    def __init__(self, *, value=7):
        self.value = value


assert KeywordOnlyInitDefault().value == 7


class VarargsKeywordOnlyInitDefault:
    def __init__(self, *items, sep=9):
        self.value = len(items) * 10 + sep


assert VarargsKeywordOnlyInitDefault(1, 2).value == 29


class KwargsInit:
    def __init__(self, a, **kwargs):
        self.value = a * 10 + kwargs["b"]


assert KwargsInit(3, b=4).value == 34


class KwargsNew:
    def __new__(cls, **kwargs):
        return kwargs["x"]


assert KwargsNew(x=7) == 7


class PositionalOnlySelf:
    def __init__(self, /, value):
        self.value = value


assert PositionalOnlySelf(value=8).value == 8


class PositionalOnlyCls:
    def __new__(cls, /, value):
        return value


assert PositionalOnlyCls(value=9) == 9


class RequiredKeywordOnlyInit:
    def __init__(self, a=1, *, b, c=3):
        self.value = a * 100 + b * 10 + c


assert RequiredKeywordOnlyInit(b=2).value == 123


class SelfDefaultInit:
    def __init__(self=0, *, value=7):
        self.value = value


assert SelfDefaultInit().value == 7


class NewWithoutInit:
    def __new__(cls):
        return 42


assert NewWithoutInit() == 42


class NewReceivesClass:
    def __new__(cls):
        return cls is NewReceivesClass


assert NewReceivesClass()


class NewPositionalDefault:
    def __new__(cls, value=7):
        return value


assert NewPositionalDefault() == 7


class NewKeywordCall:
    def __new__(cls, value=7):
        return value


assert NewKeywordCall(value=11) == 11


class NewKeywordOnlyDefault:
    def __new__(cls, *, value=7):
        return value


assert NewKeywordOnlyDefault() == 7
