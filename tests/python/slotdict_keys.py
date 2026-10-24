# CloverVM's internal attribute mapping exposes an ordered list snapshot.
class Box:
    pass


box = Box()
box.b = 1
box.a = 2
mapping = box.__dict__
keys = mapping.keys()
assert isinstance(keys, list)
assert repr(keys) == "['b', 'a']"
box.b = 3
assert repr(mapping.keys()) == "['b', 'a']"
del box.b
box.b = 4
assert repr(mapping.keys()) == "['a', 'b']"
assert repr(keys) == "['b', 'a']"
keys.append("extra")
assert len(mapping) == 2
assert repr(mapping.keys()) == "['a', 'b']"
del box
assert repr(mapping.keys()) == "['a', 'b']"
try:
    mapping.__class__.keys({})
    assert False
except TypeError:
    pass


class ClassNamespace:
    x = 1


assert "x" in ClassNamespace.__dict__.keys()
assert "keys" not in ClassNamespace.__dict__.keys()
