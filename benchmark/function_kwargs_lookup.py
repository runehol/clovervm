def add_kwargs(**kwargs):
    return kwargs["a"] * kwargs["b"] + kwargs["c"]


def run(n):
    acc = 0
    for i in range(n):
        acc += add_kwargs(c=2, a=i, b=3)
    return acc
