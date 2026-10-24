def f(a, b, *, flag, d):
    return a * b + flag + d


args = [3]
kwargs = {"d": 7}


def run(n):
    acc = 0
    for i in range(n):
        acc += f(2, *args, flag=5, **kwargs)
    return acc
