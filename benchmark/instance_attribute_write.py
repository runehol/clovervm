class Counter:
    pass


def run(n):
    obj = Counter()
    obj.value = 0
    acc = 0
    for i in range(n):
        obj.value = i
        acc += obj.value
    return acc
