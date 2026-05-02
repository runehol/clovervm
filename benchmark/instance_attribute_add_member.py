class Pair:
    def __init__(self, left, right):
        self.left = left
        self.right = right


def run(n):
    acc = 0
    for i in range(n):
        obj = Pair(i, i + 1)
        acc += obj.left + obj.right
    return acc
