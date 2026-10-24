def f(a, b, c, d, e, f, g, h, i, j):
    return a + b + c + d + e + f + g + h + i + j


def run(n):
    acc = 0
    for i in range(n):
        acc += f(j=10, i=9, h=8, g=7, f=6, e=5, d=4, c=3, b=2, a=1)
    return acc
