total = 0
for x in range(5):
    total += x
assert total == 10

total = 0
for x in range(1, 4):
    total += x
assert total == 6

total = 0
for x in range(1, 8, 3):
    total += x
assert total == 12

total = 0
for x in range(5, -1, -2):
    total += x
assert total == 9

total = 0
for x in range(3):
    for y in range(2):
        total += x + y
assert total == 9

def sum_range(n):
    total = 0
    for x in range(n):
        total += x
    return total

assert sum_range(5) == 10

def sum_pairs(n):
    total = 0
    for x in range(n):
        for y in range(2):
            total += x + y
    return total

assert sum_pairs(3) == 9

real_range = range

def range(n):
    return real_range(1, n)

total = 0
for x in range(4):
    total += x
assert total == 6
