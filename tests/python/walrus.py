# Assignment expressions evaluate once and return the assigned object.
events = []
def make_value():
    events.append('value')
    return [7]
assert (captured := make_value()) is captured
assert captured == [7] and events == ['value']
assert (outer := (inner := captured)) is captured
assert outer is inner
assert (precedence := 1 + 2 > 2 and 9) == 9
assert precedence == 9
assert (conditional := 3 if False else 4) == 4
assert conditional == 4
assert (one := 1, two := 2) == (1, 2)
assert [three := 3, four := 4] == [3, 4]
dictionary = {1: (five := 5), (six := 6): 7}
assert dictionary[1] == 5 and dictionary[6] == 7
assert (one, two, three, four, five, six) == (1, 2, 3, 4, 5, 6)

def collect(*args, **kwargs):
    return args, kwargs
assert collect(argument := captured)[0][0] is captured
assert collect(key=(keyword := 8))[1]['key'] == keyword
assert collect(*(expanded := (1, 2)))[0] == expanded
assert collect(**(mapping := {'key': 3}))[1]['key'] == mapping['key']
# Positional/starred expressions run before keyword expressions.
def call_order():
    x = 0
    result = collect(key=(x := 2), *((x := 1),))
    assert result[0] == (1,) and result[1]['key'] == 2
    assert x == 2
call_order()

data = [10, 20, 30]
assert data[index := 1] == 20 and index == 1
assert data[(start := 1):(stop := 3)] == [20, 30]
assert (start, stop) == (1, 3)
if condition := 8:
    assert condition == 8
else:
    assert False
if False:
    assert False
elif alternative := 10:
    assert alternative == 10
remaining = 3
while remaining := remaining - 1:
    assert remaining > 0
assert remaining == 0
for item in (iterable := [1, 2]):
    assert item in iterable
class Manager:
    def __enter__(self): return self
    def __exit__(self, typ, value, tb): return False
with (manager := Manager()) as entered:
    assert manager is entered

def local_scope():
    assert (x := 4) == 4 and x == 4
    assert (x := x + 1) == 5
local_scope()
x = 90
def shadows_global():
    try:
        x
    except NameError:
        pass
    else:
        assert False
    assert (x := 1) == 1
shadows_global()
assert x == 90
def updates_global():
    global x
    return (x := captured)
assert updates_global() is captured and x is captured
class ClassBinding:
    assigned = (class_local := 12)
    assert class_local == 12
assert ClassBinding.assigned == ClassBinding.class_local == 12

# Skipped operands must not turn a missing local into a proven-present local.
def skipped(flag):
    flag and (left := 1)
    flag or (right := 2)
    if flag:
        assert left == 1
        try:
            right
        except NameError:
            pass
        else:
            assert False
    else:
        assert right == 2
        try:
            left
        except NameError:
            pass
        else:
            assert False
skipped(True)
skipped(False)
def nested_shortcuts(flag):
    flag and ((a := 1) or (b := 2))
    try:
        b
    except NameError:
        pass
    else:
        assert False
nested_shortcuts(True)
nested_shortcuts(False)
def comparisons():
    assert not (2 < (first := 1) < (skipped := 3))
    assert first == 1
    try:
        skipped
    except NameError:
        pass
    else:
        assert False
    assert 0 < (middle := 1) < (last := middle + 1)
    assert last == 2
comparisons()

# Defaults and bases bind in the enclosing code object; bodies do not.
def definitions():
    def nested(value=(default := 13), *, key=(kwdefault := 14)):
        body_only = 99
        return value + key
    assert default == 13 and kwdefault == 14 and nested() == 27
    class Derived(base := object):
        body_only = 99
    assert base is object and Derived.__bases__ == (object,)
    try:
        body_only
    except NameError:
        pass
    else:
        assert False
    return default
assert definitions() == 13

def header_exceptions():
    try:
        def nested(a=(value := 21), b=1 / 0): pass
    except ZeroDivisionError:
        assert value == 21
    try:
        def nested(a=1 / 0, b=(missing := 22)): pass
    except ZeroDivisionError:
        try:
            missing
        except NameError:
            pass
        else:
            assert False
    try:
        class Derived(base := object, 1 / 0): pass
    except ZeroDivisionError:
        assert base is object
header_exceptions()
def touched_default():
    value = 1
    while value:
        del value
        def nested(arg=(value := 0)): pass
    return value
assert touched_default() == 0

def rhs_failure():
    value = [1]
    original = value
    try:
        (value := 1 / 0)
    except ZeroDivisionError:
        pass
    assert value is original
rhs_failure()

# Step 1 must retain evaluated operands across reassignment, without help from
# individual expression/assignment handlers.
def retained_operands():
    x = 10
    assert x + (x := 2) == 12 and x == 2
    x = 1
    assert x < (x := 2) and x == 2
    x = 1
    assert x < (x := 2) < 3 < 4 and x == 2
    x = [1]
    assert x in (x := [[1]])
    assert x == [[1]]
    x = [10, 20]
    assert x[(x := 1)] == 20 and x == 1
retained_operands()

def assignment_targets():
    target = [10, 20]
    original = target
    target[(target := 1)] = 30
    assert original == [10, 30] and target == 1
    value = 5
    target = [0]
    target[(value := 0)] = value
    assert target == [5] and value == 0
    value = 6
    (value := [0])[0] = value
    assert value == [6]
    target = [10, 20]
    original = target
    key = 0
    target[key] += (target := 2) + (key := 1)
    assert original == [13, 20] and target == 2 and key == 1
    target = [10, 20]
    original = target
    target[(target := 0)] += 3
    assert original == [13, 20] and target == 0
    target = [10, 20]
    original = target
    del target[(target := 0)]
    assert original == [20] and target == 0
    target = [0]
    original = target
    with Manager() as target[(target := 0)]:
        pass
    assert isinstance(original[0], Manager) and target == 0
    target = Manager()
    target.value = 10
    original = target
    target.value += (target := 2)
    assert original.value == 12 and target == 2
assignment_targets()

# The target cannot define a local before the RHS reads it, even when annotated.
def rhs_before_subscript_target():
    try:
        (value := [0])[0] = value
    except NameError:
        pass
    else:
        assert False
rhs_before_subscript_target()
def rhs_before_attribute_target():
    try:
        (value := Manager()).attribute = value
    except NameError:
        pass
    else:
        assert False
rhs_before_attribute_target()
def rhs_before_annotated_target():
    try:
        (value := Manager()).attribute: int = value
    except NameError:
        pass
    else:
        assert False
rhs_before_annotated_target()
def rhs_defines_target_receiver():
    value[0] = (value := [0])
    assert value[0] is value
    other[0]: int = (other := [0])
    assert other[0] is other
rhs_defines_target_receiver()

class Operand:
    def __add__(self, other): return self.value + other

def retained_lifetime():
    value = Operand()
    value.value = 40
    return value + (value := 2)
assert retained_lifetime() == 42
