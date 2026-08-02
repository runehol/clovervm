def fib(n):
    a = 0
    b = 1
    i = 0
    while i is not n:
        next_value = a + b
        a = b
        b = next_value
        i += 1
    return a


def run(n):
    acc = 0
    for i in range(n):
        acc ^= fib(80)
    return acc


run(3000000)
