def add_varargs(a, *args):
    return a + args[0]


def run(n):
    acc = 0
    for i in range(n):
        acc += add_varargs(i, 1)
    return acc
