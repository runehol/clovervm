class Pair:
    pass


def run(n):
    obj = Pair()
    obj.left = 1
    obj.right = 2
    acc = 0
    for i in range(n):
        acc += obj.left + obj.right
    return acc
