def add_default(a, b=1, c=2):
    return a * b + c


def run(n):
    acc = 0
    for i in range(n):
        acc += add_default(b=3, a=i)
    return acc
