class Pair:
    pass


def run(n):
    acc = 0
    for i in range(n):
        obj = Pair()
        obj.left = i
        obj.right = i + 1
        acc += obj.left + obj.right
    return acc
