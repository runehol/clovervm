def f(a, b, c):
    return a * b + c


def run(n):
    acc = 0
    for i in range(n):
        acc += f(2, 3, 5)
    return acc
