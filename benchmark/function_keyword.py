def add3(a, b, c):
    return a * b + c


def run(n):
    acc = 0
    for i in range(n):
        acc += add3(c=2, a=i, b=3)
    return acc
