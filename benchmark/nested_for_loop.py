def run(n):
    acc = 0
    for x in range(n):
        for y in range(10):
            acc += x * y
    return acc
