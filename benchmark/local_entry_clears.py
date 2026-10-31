def definite(value):
    a = value
    b = value
    c = value
    d = value
    e = value
    f = value
    g = value
    h = value
    return value


def mixed_one(value):
    if value & 1:
        a = value
    return value


def mixed_eight(value):
    if value & 1:
        a = value
        b = value
        c = value
        d = value
        e = value
        f = value
        g = value
        h = value
    return value


def run(mode):
    if mode == 0:
        operation = definite
    elif mode == 1:
        operation = mixed_one
    else:
        operation = mixed_eight
    total = 0
    for i in range(10000):
        total += operation(i)
    return total
