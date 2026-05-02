def add3(a, b, c):
    return a + b + c

assert add3(1, 2, 3) == 6

def add4(a, b, c, d):
    return a + b + c + d

assert add4(1, 2, 3, 4) == 10

def add(a, b=10, c=100):
    return a + b + c

assert add(1) + add(1, 2) + add(1, 2, 3) == 220

x = 10

def default_from_definition_time(a=x + 1):
    return a

x = 20
assert default_from_definition_time() == 11

def varargs(a, *args):
    return a + args[0] + args[1]

assert varargs(1, 4, 5) == 10

def defaults_and_varargs(a, b=10, *args):
    return a + b + args[0] + args[1]

assert defaults_and_varargs(1, 20, 21, 23) == 65

def defaults_and_empty_varargs(a, b=10, *args):
    return a + b

assert defaults_and_empty_varargs(1) == 11

def add3_trailing(a, b, c,):
    return a + b + c

assert add3_trailing(1, 2, 3,) == 6

def implicit_none():
    a = 1

assert implicit_none() is None

def pick(n):
    if n:
        while n:
            return 1
    else:
        return 2

assert pick(0) == 2

def fib(n):
    if n <= 2:
        return n
    return fib(n - 2) + fib(n - 1)

assert fib(20) == 10946
