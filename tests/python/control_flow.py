def maybe_write(flag):
    if flag:
        value = 7
    else:
        value = 8
    return value

assert maybe_write(False) == 8

# Float truthiness drives assertions, branches, and loop conditions.
assert 1.0
assert not 0.0

if 1.0:
    float_if_result = 1
else:
    float_if_result = 2
assert float_if_result == 1

if 0.0:
    float_if_result = 1
else:
    float_if_result = 2
assert float_if_result == 2

float_while_guard = 1.0
float_while_count = 0
while float_while_guard:
    float_while_count += 1
    float_while_guard = 0.0
assert float_while_count == 1

b = 0
a = 100
while a:
    a -= 1
    b += a
assert b == 4950

a = False
b = True
if a:
    branch = 1
elif b:
    branch = 2
else:
    branch = 3
assert branch == 2

a = False
b = False
if a:
    branch = 1
elif b:
    branch = 2
else:
    branch = 3
assert branch == 3

a = 2
b = 0
while a:
    a -= 1
else:
    b = 7
assert b == 7

a = 2
b = 0
while a:
    break
else:
    b = 7
assert b == 0

total = 0
for x in range(3):
    total += x
else:
    total += 10
assert total == 13

total = 0
for x in range(5):
    if x == 3:
        break
    total += x
else:
    total += 100
assert total == 3

total = 0
for x in range(5):
    if x == 2:
        continue
    total += x
assert total == 8

# Boolean operators return operand values and skip unneeded right-hand sides.
assert (0 and missing) == 0
assert (1 and 7) == 7
assert (5 or missing) == 5
assert (0 or 8) == 8


def boolean_rhs_must_not_run():
    raise ValueError


if True or boolean_rhs_must_not_run():
    boolean_shortcut_result = 4
else:
    boolean_shortcut_result = 1
assert boolean_shortcut_result == 4

if False and boolean_rhs_must_not_run():
    boolean_shortcut_result = 1
else:
    boolean_shortcut_result = 4
assert boolean_shortcut_result == 4
