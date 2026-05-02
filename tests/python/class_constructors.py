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
