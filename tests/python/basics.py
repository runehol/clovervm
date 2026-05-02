assert 1 + 2 * (4 + 3) == 15
assert (1 << 4) + 3 == 19
assert (not True) == False
assert 1 - 2 * (4 + 3) == -13

a = 4
assert a + 3 == 7
a += 7
assert a == 11

def maybe_write(flag):
    if flag:
        value = 7
    else:
        value = 8
    return value

assert maybe_write(False) == 8

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
