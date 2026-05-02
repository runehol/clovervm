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
