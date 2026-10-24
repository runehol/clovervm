# Shared items() lists and malformed rows, compared with CPython 3.14.5.
# These are original tests, not adapted from CPython's test suite.
import json


class Unsupported:
    pass


class Mapping(dict):
    def items(self):
        return rows


obj = Unsupported()
rows = []


def append_row(value):
    rows.append(("c", 3))
    return 1


def delete_row(value):
    rows.pop(1)
    return 1


for sorting in (False, True):
    rows = [("a", obj), ("b", 2)]
    assert json.dumps(Mapping({"real": 1}), default=append_row,
                      sort_keys=sorting) == '{"a": 1, "b": 2, "c": 3}'
    assert len(rows) == 3
    rows = [("a", obj), ("b", 2)]
    assert json.dumps(Mapping({"real": 1}), default=delete_row,
                      sort_keys=sorting) == '{"a": 1}'
    assert len(rows) == 1

rows = [("z", 1), ("a", 2)]
assert json.dumps(Mapping({"real": 1}), sort_keys=True) == '{"a": 2, "z": 1}'
assert rows == [("a", 2), ("z", 1)]
# A list subclass is materialized, so it does not share the exact-list behavior.
class RowList(list):
    pass


rows = RowList([("a", obj), ("b", 2)])
assert json.dumps(Mapping({"real": 1}), default=append_row) == '{"a": 1, "b": 2}'
rows = RowList([("z", 1), ("a", 2)])
assert json.dumps(Mapping({"real": 1}), sort_keys=True) == '{"a": 2, "z": 1}'
assert rows[0][0] == "z"

observations = []


class ObservedKey(str):
    def __lt__(self, other):
        observations.append(len(rows))
        return str.__lt__(self, other)


rows = [(ObservedKey("z"), 1), (ObservedKey("a"), 2)]
assert json.dumps(Mapping({"real": 1}), sort_keys=True) == '{"a": 2, "z": 1}'
assert len(observations) > 0
for count in observations:
    assert count == 0
assert rows[0][0] == "a"


class MutatingKey(str):
    def __lt__(self, other):
        rows.append(("new", 3))
        return str.__lt__(self, other)


rows = [(MutatingKey("z"), 1), (MutatingKey("a"), 2)]
try:
    json.dumps(Mapping({"real": 1}), sort_keys=True)
except ValueError as error:
    assert str(error) == "list modified during sort"
else:
    assert False
assert len(rows) == 2
assert rows[0][0] == "a"


class RaisingKey(str):
    def __lt__(self, other):
        raise KeyError("comparison")


rows = [(RaisingKey("z"), 1), (RaisingKey("a"), 2)]
try:
    json.dumps(Mapping({"real": 1}), sort_keys=True)
except KeyError as error:
    assert error.args == ("comparison",)
else:
    assert False
assert len(rows) == 2
assert rows[0][0] == "z"

# Comparable list rows sort first, then fail the tuple-only validation contract.
for candidate in ([['b', 1], ['a', 2]], [[], ['a', 2]],
                  [[['b'], 1], [['a'], 2]]):
    rows = candidate
    try:
        json.dumps(Mapping({"real": 1}), sort_keys=True)
    except ValueError as error:
        assert str(error) == "items must return 2-tuples"
    else:
        assert False
    assert rows[0] == [] or rows[0][0] == 'a' or rows[0][0] == ['a']

# Mixed tuple/list rows remain incomparable, so sorting itself raises TypeError.
rows = [("a", 1), ["b", 2]]
try:
    json.dumps(Mapping({"real": 1}), sort_keys=True)
except TypeError:
    pass
else:
    assert False
assert len(rows) == 2
assert json.dumps({"still": "works"}) == '{"still": "works"}'
