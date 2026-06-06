def run(n):
    values = {"alpha": 29, "beta": 31, "gamma": 37, "delta": 41}
    acc = 0
    i = 0
    idx = 0
    while i < n:
        if idx == 0:
            key = "alpha"
        elif idx == 1:
            key = "beta"
        elif idx == 2:
            key = "gamma"
        else:
            key = "delta"
        acc += values[key]
        idx += 1
        if idx == 4:
            idx = 0
        i += 1
    return acc
