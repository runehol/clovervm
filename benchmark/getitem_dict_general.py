def run(n):
    values = {"alpha": 29, "beta": 31, "gamma": 37, "delta": 41}
    # Promote without changing the table contents.
    0 in values
    acc = 0
    i = 0
    while i + 4 <= n:
        acc += values["alpha"]
        acc += values["beta"]
        acc += values["gamma"]
        acc += values["delta"]
        i += 4
    while i < n:
        acc += values["alpha"]
        i += 1
    return acc
