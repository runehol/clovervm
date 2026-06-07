def run(n):
    values = "abcd"
    acc = 0
    i = 0
    item = ""
    while i < n:
        item = values[0]
        acc += 1
        i += 1
    if item == "a":
        return acc
    return acc
