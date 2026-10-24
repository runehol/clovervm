# JSON live dictionary and subclass regressions, compared with CPython 3.14.5.
# These are original tests, not adapted from CPython's test suite.
import json


class Unsupported:
    pass


obj = Unsupported()
source = {"a": obj, "b": 2}


def add_later(value):
    source["c"] = 3
    return 1


assert json.dumps(source, default=add_later) == '{"a": 1, "b": 2, "c": 3}'
source = {"a": obj, "b": 2}


def remove_later(value):
    del source["b"]
    return 1


assert json.dumps(source, default=remove_later) == '{"a": 1}'
source = {"a": obj, "b": 2, "c": 3}


def replace_later(value):
    source["b"] = 99
    return 1


assert json.dumps(source, default=replace_later) == '{"a": 1, "b": 99, "c": 3}'
source = {"a": obj, "b": 2, "c": 3}


def remove_earlier_and_append(value):
    del source["a"]
    source["d"] = 4
    return 1


assert json.dumps(source, default=remove_earlier_and_append) == (
    '{"a": 1, "b": 2, "c": 3, "d": 4}'
)
source = {"a": obj, "b": 2, "c": 3}


def move_later(value):
    del source["b"]
    source["b"] = 4
    return 1


assert json.dumps(source, default=move_later) == '{"a": 1, "c": 3, "b": 4}'
source = {"a": obj, "b": 2}


def add_cycle(value):
    source["c"] = source
    return 1


try:
    json.dumps(source, default=add_cycle)
except ValueError as error:
    assert str(error) == "Circular reference detected"
else:
    assert False


class KeysOverride(dict):
    def keys(self):
        raise AssertionError("keys override must not be called")


assert json.dumps(KeysOverride({"x": 1})) == '{"x": 1}'
assert json.dumps(KeysOverride({"x": 1}), sort_keys=True) == '{"x": 1}'


class ItemsOverride(dict):
    def items(self):
        return [("z", 7), ("a", 8)]

    def keys(self):
        raise AssertionError("must enumerate items")

    def __getitem__(self, key):
        raise AssertionError("must use snapshotted values")


mapping = ItemsOverride({"real": 1})
assert json.dumps(mapping) == '{"z": 7, "a": 8}'
assert json.dumps(mapping, sort_keys=True) == '{"a": 8, "z": 7}'
# CPython bypasses items() for an underlying empty dict, even a subclass.
assert json.dumps(ItemsOverride()) == '{}'


class RepeatedItems(dict):
    def items(self):
        return [("x", 3), ("x", 1), ("x", 2)]


assert json.dumps(RepeatedItems({"real": 1})) == '{"x": 3, "x": 1, "x": 2}'
assert json.dumps(RepeatedItems({"real": 1}), sort_keys=True) == '{"x": 1, "x": 2, "x": 3}'


class SubclassSnapshot(dict):
    pass


source = SubclassSnapshot({"a": obj, "b": 2})
assert json.dumps(source, default=add_later) == '{"a": 1, "b": 2}'
assert source["c"] == 3


class BadItems(dict):
    def items(self):
        return [["x", 1]]


for sorting in (False, True):
    try:
        json.dumps(BadItems({"real": 1}), sort_keys=sorting)
    except ValueError as error:
        assert str(error) == "items must return 2-tuples"
    else:
        assert False

# Empty dictionaries bypass circularity markers, even if a callback emptied an
# active ancestor. This uses builtin storage size rather than subclass hooks.
items_calls = []


class EmptyCheckMapping(dict):
    def __len__(self):
        raise AssertionError("must use builtin dictionary size")

    def items(self):
        items_calls.append(1)
        return dict.items(self)


for mapping_class in (dict, SubclassSnapshot, EmptyCheckMapping):
    for sorting in (False, True):
        for circular in (False, True):
            for repeated in (False, True):
                source = mapping_class({"a": obj})
                items_calls = []
                callback_calls = []

                def empty_ancestor(value):
                    callback_calls.append(value)
                    source.clear()
                    if repeated:
                        return [source, source]
                    return source

                expected = '{"a": {}}'
                if repeated:
                    expected = '{"a": [{}, {}]}'
                assert json.dumps(source, default=empty_ancestor,
                                  sort_keys=sorting,
                                  check_circular=circular) == expected
                assert dict.__len__(source) == 0
                assert callback_calls == [obj]
                if mapping_class is EmptyCheckMapping:
                    assert items_calls == [1]
                else:
                    assert items_calls == []

# A nonempty ancestor still trips the marker before any second items() call.
for sorting in (False, True):
    source = EmptyCheckMapping({"a": obj})
    items_calls = []

    def nonempty_ancestor(value):
        return source

    try:
        json.dumps(source, default=nonempty_ancestor, sort_keys=sorting)
    except ValueError as error:
        assert str(error) == "Circular reference detected"
    else:
        assert False
    assert items_calls == [1]

# Empty subclass instances never consult either override at the root.
items_calls = []
assert json.dumps(EmptyCheckMapping()) == '{}'
assert json.dumps(EmptyCheckMapping(), sort_keys=True) == '{}'
assert items_calls == []
