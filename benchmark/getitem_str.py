def run(n):
    values = "abcd"
    acc = 0
    i = 0
    idx = 0
    while i < n:
        item = values[idx]
        if item == "a":
            acc += 43
        elif item == "b":
            acc += 47
        elif item == "c":
            acc += 53
        else:
            acc += 59
        idx += 1
        if idx == 4:
            idx = 0
        i += 1
    return acc
