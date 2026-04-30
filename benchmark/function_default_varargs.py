def add_default_varargs(a, b=10, *args):
    return a + b


def run(n):
    acc = 0
    for i in range(n):
        acc += add_default_varargs(i)
    return acc
