class Pair:
    left = 1
    right = 2


def run(n):
    acc = 0
    for i in range(n):
        acc += Pair.left + Pair.right
    return acc
