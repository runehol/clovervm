items = (1, 2, 3)


def run(n):
    acc = 0
    i = 0
    while i < n:
        acc += len(items)
        i += 1
    return acc
