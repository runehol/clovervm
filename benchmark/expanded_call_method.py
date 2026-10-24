class Calculator:
    def f(self, a, b, c):
        return a * b + c


obj = Calculator()
args = (2, 3)
kwargs = {"c": 5}


def run(n):
    acc = 0
    for i in range(n):
        acc += obj.f(*args, **kwargs)
    return acc
