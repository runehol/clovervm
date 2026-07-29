triple = 1, 2, 4
assert triple[0] == 1
assert triple[1] == 2
assert triple[2] == 4

# Tuple literals produce tuples and evaluate elements from left to right.
assert len(()) == 0


tuple_literal_order = 0


def next_tuple_literal_value():
    global tuple_literal_order
    value = tuple_literal_order
    tuple_literal_order += 1
    return value


ordered_tuple = (
    next_tuple_literal_value(),
    next_tuple_literal_value(),
    next_tuple_literal_value(),
)
assert ordered_tuple[0] == 0
assert ordered_tuple[1] == 1
assert ordered_tuple[2] == 2

paren_triple = (1, 2, 4)
assert paren_triple[0] == 1
assert paren_triple[1] == 2
assert paren_triple[2] == 4
assert paren_triple.__getitem__(1) == paren_triple[1]
assert paren_triple.__getitem__(-1) == paren_triple[-1]

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
