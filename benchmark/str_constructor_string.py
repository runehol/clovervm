def run(n):
    value = "abcdef"
    acc = 0
    for _ in range(n):
        s = str(value)
        acc += len(s)
    return acc
