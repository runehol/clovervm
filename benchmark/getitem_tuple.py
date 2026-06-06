def run(n):
    values = (13, 17, 19, 23)
    acc = 0
    i = 0
    idx = 0
    while i < n:
        acc += values[idx]
        idx += 1
        if idx == 4:
            idx = 0
        i += 1
    return acc
