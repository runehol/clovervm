class DefaultSource:
    x = 10

    def method(self, a=x + 1):
        return a

assert DefaultSource().method() == 11

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
