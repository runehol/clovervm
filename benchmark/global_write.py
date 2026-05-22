x = 0


def run(n):
    global x
    acc = 0
    for i in range(n):
        x = i
        acc += x
    return acc
