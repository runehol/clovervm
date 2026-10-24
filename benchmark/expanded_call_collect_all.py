def f(**kwargs):
    return kwargs["a"] * kwargs["b"] + kwargs["c"]


kwargs = {"c": 5, "a": 2, "b": 3}


def run(n):
    acc = 0
    for i in range(n):
        acc += f(**kwargs)
    return acc
