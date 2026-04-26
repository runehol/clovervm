class Counter:
    value = 0


def run(n):
    acc = 0
    for i in range(n):
        Counter.value = i
        acc += Counter.value
    return acc
