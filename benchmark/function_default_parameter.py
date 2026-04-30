def add_default(value, increment=1):
    return value + increment


def run(n):
    acc = 0
    for i in range(n):
        acc += add_default(i)
    return acc
