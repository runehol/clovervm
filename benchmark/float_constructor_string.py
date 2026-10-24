def run(n):
    value = "12345.5"
    acc = 0
    for _ in range(n):
        converted = float(value)
        if converted == 12345.5:
            acc += 1
    return acc
