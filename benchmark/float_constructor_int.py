def run(n):
    value = 12345
    acc = 0
    for _ in range(n):
        converted = float(value)
        if converted == value:
            acc += 1
    return acc
