def f(a, b, c):
    return a * b + c


args = (2,)
kwargs = {"c": 5, "b": 3}


def run(n):
    acc = 0
    for i in range(n):
        acc += f(*args, **kwargs)
    return acc
