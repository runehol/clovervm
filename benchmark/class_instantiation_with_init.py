class Point:
    def __init__(self, value):
        self.value = value


def run(n):
    acc = 0
    for i in range(n):
        Point(i)
        acc += 1
    return acc
