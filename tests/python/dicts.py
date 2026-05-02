key = "alpha"
items = {key: 4, "beta": 7}
assert items[key] == 4

items["beta"] = 11
assert items["beta"] == 11

del items["beta"]
assert items["alpha"] == 4

items = {"alpha": 4, "beta": 7}
items["beta"] += 5
assert items["beta"] == 12
