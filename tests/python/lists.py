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

xs = [1, 2]
assert xs.append(3) is None
assert xs[2] == 3

xs.extend([4, 5])
assert xs[3] == 4
assert xs[4] == 5

xs.extend((6, 7))
assert xs[5] == 6
assert xs[6] == 7

self_extend = [1, 2]
self_extend.extend(self_extend)
assert len(self_extend) == 4
assert self_extend[0] == 1
assert self_extend[1] == 2
assert self_extend[2] == 1
assert self_extend[3] == 2

assert xs.count(2) == 1
assert xs.count(99) == 0
assert xs.index(4) == 3
assert xs.index(6, -2) == 5

xs.insert(1, 10)
assert xs[0] == 1
assert xs[1] == 10
assert xs[2] == 2

assert xs.pop() == 7
assert xs.pop(1) == 10
assert xs[1] == 2

xs.remove(4)
assert xs[2] == 3
assert xs[3] == 5

copy = xs.copy()
copy[0] = 99
assert xs[0] == 1
assert copy[0] == 99

xs.reverse()
assert xs[0] == 6
assert xs[1] == 5

joined = [1, 2].__add__([3])
assert joined[0] == 1
assert joined[1] == 2
assert joined[2] == 3

xs.clear()
assert len(xs) == 0
