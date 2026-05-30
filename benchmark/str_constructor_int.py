def run(n):
    acc = 0
    for i in range(n):
        s = str(i)
        acc += len(s)
    return acc
