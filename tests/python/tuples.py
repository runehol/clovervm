triple = 1, 2, 4
assert triple[0] == 1
assert triple[1] == 2
assert triple[2] == 4

paren_triple = (1, 2, 4)
assert paren_triple[0] == 1
assert paren_triple[1] == 2
assert paren_triple[2] == 4

items = ("alpha", "beta", "alpha", "gamma")
assert items.count("alpha") == 2
assert items.count("missing") == 0
assert items.index("alpha") == 0
assert items.index("alpha", 1) == 2
assert items.index("gamma", -1) == 3

joined = ("a", "b").__add__(("c",))
assert joined[0] == "a"
assert joined[1] == "b"
assert joined[2] == "c"
