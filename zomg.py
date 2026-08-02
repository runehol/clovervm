def fib(n):
    a = 0
    b = 1
    i = 0
    while i < n:
        next_value = a + b
        a = b
        b = next_value
        i += 1
    return a

