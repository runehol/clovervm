# None hashing and dictionary behavior, compared with CPython 3.14.5.
# These are original tests, not adapted from CPython's test suite.

assert hash(None) == 4238894112
assert None.__hash__() == hash(None)
none_type = None.__class__
assert none_type.__hash__(None) == hash(None)
for value in (0, False, "", [], {}):
    try:
        none_type.__hash__(value)
    except TypeError:
        pass
    else:
        assert False


def hash_one(value):
    return hash(value)


# Reuse the same call site across None, int and bool to exercise cache replay
# and shape guards without allowing the None handler to accept other values.
idx = 0
while idx < 40:
    assert hash_one(None) == 4238894112
    assert hash_one(17) == 17
    assert hash_one(False) == 0
    assert hash_one(None) == 4238894112
    idx += 1

# The integer hash collision must preserve distinct keys in either order.
collision = hash(None)
for table in ({None: "none", collision: "integer"},
              {collision: "integer", None: "none"}):
    assert len(table) == 2
    assert table[None] == "none"
    assert table[collision] == "integer"
    assert None in table
    idx = 0
    while idx < 64:
        table[idx] = idx
        table[None] = idx
        assert table[None] == idx
        assert table[collision] == "integer"
        idx += 1
    assert len(table) == 66
    assert table[False] == 0
    assert table[True] == 1
    del table[None]
    assert None not in table
    assert table[collision] == "integer"
    table[None] = "reinserted"
    assert table.get(None) == "reinserted"
    del table[collision]
    assert table[None] == "reinserted"
    assert table[False] == 0
