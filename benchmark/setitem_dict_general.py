def run(n):
    values = {"alpha": 0, "beta": 0, "gamma": 0, "delta": 0}
    # Promote without changing the table contents.
    0 in values
    i = 0
    while i + 4 <= n:
        values["alpha"] = i
        values["beta"] = i + 1
        values["gamma"] = i + 2
        values["delta"] = i + 3
        i += 4
    while i < n:
        values["alpha"] = i
        i += 1
    return values["alpha"] + values["beta"] + values["gamma"] + values["delta"]
