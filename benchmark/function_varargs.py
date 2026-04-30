def add_varargs(*args):
    return args[0] + args[1]


def run(n):
    acc = 0
    for i in range(n):
        acc += add_varargs(i, 1)
    return acc
