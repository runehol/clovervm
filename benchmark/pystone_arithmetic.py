class Context:
    pass


def proc7(a, b):
    return a + b + 2


def proc2(seed, limit):
    total = 0
    i = 0
    while i < limit:
        total += proc7(seed, i)
        if total > 1000:
            total -= 333
        else:
            total += 17
        i += 1
    return total


def proc6(tag):
    if tag == 1:
        return 2
    if tag == 2:
        return 3
    if tag == 3:
        return 1
    return 1


def proc1(ctx, seed):
    acc = seed
    outer = 0
    while outer < 5:
        acc += proc2(acc + outer, 6)
        if acc > 5000:
            acc -= 777
        else:
            acc += 111
        outer += 1
    ctx.tag = proc6(ctx.tag)
    ctx.accum = ctx.accum + acc
    return acc + ctx.tag


def run(n):
    ctx = Context()
    ctx.tag = 1
    ctx.accum = 0

    total = 0
    i = 0
    seed = 3
    while i < n:
        total += proc1(ctx, seed)
        total += proc2(seed, 4)
        if total > 20000:
            total -= 5000
        else:
            total += ctx.tag

        seed += 1
        if seed > 8:
            seed = 3
        i += 1

    return total + ctx.accum + ctx.tag
