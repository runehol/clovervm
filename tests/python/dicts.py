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

items = {"alpha": 4, "beta": 7}
assert items.get("alpha") == 4
assert items.get("missing") is None
assert items.get("missing", 99) == 99

keys = items.keys()
assert keys[0] == "alpha"
assert keys[1] == "beta"

values = items.values()
assert values[0] == 4
assert values[1] == 7

pairs = items.items()
assert pairs[0][0] == "alpha"
assert pairs[0][1] == 4
assert pairs[1][0] == "beta"
assert pairs[1][1] == 7

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
