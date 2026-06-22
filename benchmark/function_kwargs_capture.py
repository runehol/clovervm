def count_kwargs(**kwargs):
    return 1


def run(n):
    acc = 0
    for i in range(n):
        acc += count_kwargs(c=2, a=i, b=3)
    return acc
