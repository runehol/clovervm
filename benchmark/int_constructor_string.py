def run(n):
    value = "12345"
    acc = 0
    for _ in range(n):
        converted = int(value)
        acc += converted
    return acc
