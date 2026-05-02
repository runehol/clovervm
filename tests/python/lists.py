xs = [1, 2, 4]
assert xs[0] == 1
assert xs[1] == 2
assert xs[2] == 4

xs = [4, 7, 9]
assert xs[1] == 7

xs[1] = 11
assert xs[1] == 11

del xs[1]
assert xs[1] == 9

xs = [4, 7, 9]
del xs[-1]
assert xs[1] == 7

xs = [4, 7, 9]
xs[1] += 5
assert xs[1] == 12
