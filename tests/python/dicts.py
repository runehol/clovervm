key = "alpha"
items = {key: 4, "beta": 7}
assert items[key] == 4
assert items.__getitem__(key) == items[key]
assert items.__getitem__("beta") == items["beta"]

items["beta"] = 11
assert items["beta"] == 11

del items["beta"]
assert items["alpha"] == 4

items = {"alpha": 4, "beta": 7}
items["beta"] += 5
assert items["beta"] == 12

items = {"alpha": 4, "beta": 7}
assert items.get("alpha") == 4
assert items.get("missing") is None
assert items.get("missing", 99) == 99

keys = items.keys()
assert len(keys) == 2
key_iter = iter(keys)
assert next(key_iter) == "alpha"
assert next(key_iter) == "beta"

values = items.values()
assert len(values) == 2
value_iter = iter(values)
assert next(value_iter) == 4
assert next(value_iter) == 7

pairs = items.items()
assert len(pairs) == 2
pair_iter = iter(pairs)
first_pair = next(pair_iter)
assert first_pair[0] == "alpha"
assert first_pair[1] == 4
second_pair = next(pair_iter)
assert second_pair[0] == "beta"
assert second_pair[1] == 7

items["gamma"] = 9
assert len(keys) == 3
del items["gamma"]

copy = items.copy()
copy["alpha"] = 40
assert items["alpha"] == 4
assert copy["alpha"] == 40

items.update({"beta": 70, "gamma": 9})
assert items["alpha"] == 4
assert items["beta"] == 70
assert items["gamma"] == 9

assert items.setdefault("gamma", 99) == 9
assert items.setdefault("delta", 10) == 10
assert items["delta"] == 10

assert items.pop("delta") == 10
assert items.get("delta") is None

last = items.popitem()
assert last[0] == "gamma"
assert last[1] == 9
assert items.get("gamma") is None

from_tuple = dict.fromkeys(("left", "right"))
assert from_tuple["left"] is None
assert from_tuple["right"] is None

from_list = dict.fromkeys(["left", "right"], 3)
assert from_list["left"] == 3
assert from_list["right"] == 3

items.clear()
assert len(items) == 0

joined = ("a", "b").__add__(("c",))
assert joined[0] == "a"
assert joined[1] == "b"
assert joined[2] == "c"
