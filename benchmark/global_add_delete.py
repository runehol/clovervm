def run(n):
    global value
    acc = 0
    for i in range(n):
        value = i
        acc += value
        del value
    return acc
