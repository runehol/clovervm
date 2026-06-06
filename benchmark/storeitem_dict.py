def run(n):
    values = {"alpha": 0, "beta": 0, "gamma": 0, "delta": 0}
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
        values[key] = i
        idx += 1
        if idx == 4:
            idx = 0
        i += 1
    return values["alpha"] + values["beta"] + values["gamma"] + values["delta"]
