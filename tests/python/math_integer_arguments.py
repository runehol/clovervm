import math


class PlainObject:
    pass


invalid_values = (2.0, 1.5, -1.5, "2", None, PlainObject())
for function in (math.factorial, math.isqrt):
    for value in invalid_values:
        raised = False
        try:
            function(value)
        except TypeError:
            raised = True
        assert raised
    raised = False
    try:
        function(-1)
    except ValueError:
        raised = True
    assert raised

for function in (math.comb, math.perm):
    for value in invalid_values:
        raised = False
        try:
            function(value, -1)
        except TypeError:
            raised = True
        assert raised
        if value is not None or function is math.comb:
            # perm's None means its optional second argument is omitted.
            raised = False
            try:
                function(-1, value)
            except TypeError:
                raised = True
            assert raised
    for arguments in ((-1, 1), (1, -1)):
        raised = False
        try:
            function(arguments[0], arguments[1])
        except ValueError:
            raised = True
        assert raised

for function in (math.gcd, math.lcm):
    for value in invalid_values:
        raised = False
        try:
            function(value, 2)
        except TypeError:
            raised = True
        assert raised
        raised = False
        try:
            function(0, value)
        except TypeError:
            raised = True
        assert raised

for value in (False, True):
    for function in (math.factorial, math.isqrt, math.gcd, math.lcm):
        result = function(value)
        assert result.__class__ is int
    assert math.isqrt(value) == int(value)
    assert math.gcd(value) == int(value)
    assert math.lcm(value) == int(value)
    assert math.factorial(value) == 1

for arguments in ((True, True), (True, False), (False, False), (False, True)):
    for function in (math.comb, math.perm):
        result = function(arguments[0], arguments[1])
        assert result.__class__ is int
        assert result == function(int(arguments[0]), int(arguments[1]))

assert math.perm(True) == 1
assert math.perm(3, None) == 6
assert math.gcd(-12, 18) == 6
assert math.lcm(-12, 18) == 36
assert math.factorial(21) == 51090942171709440000
assert math.comb(10**30, 1) == 10**30
assert math.perm(10**30, 1) == 10**30
assert math.isqrt(10**40) == 10**20
assert math.gcd(10**30, 10**20) == 10**20
assert math.lcm(10**30, 10**20) == 10**30

# The builtin-only scope deliberately does not implement __index__ yet.
class IndexObject:
    def __index__(self):
        return 2


for function in (math.factorial, math.isqrt, math.gcd, math.lcm, math.comb, math.perm):
    raised = False
    try:
        if function is math.comb:
            function(IndexObject(), 1)
        else:
            function(IndexObject())
    except TypeError:
        raised = True
    assert raised
