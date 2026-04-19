class Empty:
    pass


def run(n):
    acc = 0
    for _ in range(n):
        Empty()
        acc += 1
    return acc
