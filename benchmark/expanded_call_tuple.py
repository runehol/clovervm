def f(a, b, c):
    return a * b + c


args = (2, 3, 5)


def run(n):
    acc = 0
    for i in range(n):
        acc += f(*args)
    return acc
