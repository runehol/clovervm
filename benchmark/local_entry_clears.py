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


def loop_one(value):
    while value & 1:
        a = value
        break
    return value


def loop_eight(value):
    while value & 1:
        a = value
        b = value
        c = value
        d = value
        e = value
        f = value
        g = value
        h = value
        break
    return value


def loop_target(value):
    for item in range(value & 1):
        pass
    return value


def loop_definite(value):
    a = value
    while value & 1:
        a = value
        break
    return value


def run(mode):
    if mode == 0:
        operation = definite
    elif mode == 1:
        operation = mixed_one
    elif mode == 2:
        operation = mixed_eight
    elif mode == 3:
        operation = loop_one
    elif mode == 4:
        operation = loop_eight
    elif mode == 5:
        operation = loop_target
    else:
        operation = loop_definite
    total = 0
    for i in range(10000):
        total += operation(i)
    return total
