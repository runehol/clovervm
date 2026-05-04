class Box:
    pass


def run(n):
    obj = Box()
    a = Box()
    b = Box()
    acc = 0
    for i in range(n):
        obj.value = a
        obj.value = b
        acc += i
    return acc
