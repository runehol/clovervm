class Box:
    pass


x = None


def run(n):
    global x
    a = Box()
    b = Box()
    acc = 0
    for i in range(n):
        x = a
        x = b
        acc += i
    return acc
