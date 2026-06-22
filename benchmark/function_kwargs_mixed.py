def add_kwargs_mixed(a, b=1, *, c=2, **kwargs):
    return a * b + c + kwargs["d"] + kwargs["e"]


def run(n):
    acc = 0
    for i in range(n):
        acc += add_kwargs_mixed(i, e=7, c=2, d=5, b=3)
    return acc
