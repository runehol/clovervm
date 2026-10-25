# Original regressions checked against CPython 3.14.5 Modules/_json.c.
# Use accelerated dumps as the oracle: Python fallback iterencode differs in
# shared-list sorting and malformed-row errors. The host wrapper reuses this
# source against both that oracle and CloverVM's production Python encoder.
import json


class RaisingRow(tuple):
    def __len__(self):
        raise AssertionError("overridden row length")

    def __getitem__(self, index):
        raise AssertionError("overridden row indexing")


class InheritedRow(RaisingRow):
    pass


class MisleadingRow(tuple):
    def __len__(self):
        return 2

    def __getitem__(self, index):
        return ("wrong", 99)[index]


class MisleadingLengthRow(tuple):
    def __len__(self):
        return 0


class Mapping(dict):
    def items(self):
        return self.rows


def mapping(rows):
    # Use the supported inherited dict constructor; custom dict initialization
    # is a separate VM gap. Empty stored dicts would bypass items().
    result = Mapping({"real": 1})
    result.rows = rows
    return result


class Unsupported:
    pass


class ComparisonError(Exception):
    pass


class SortRow(RaisingRow):
    def __lt__(self, other):
        raise ComparisonError("sorting first")


class ReverseRow(InheritedRow):
    def __lt__(self, other):
        return tuple.__getitem__(self, 0) > tuple.__getitem__(other, 0)


rows = []
calls = []
active_encoder = None


def callback(value):
    calls.append("default")
    return 1


def append_row(value):
    rows.append(InheritedRow(("c", 3)))
    return 1


def delete_row(value):
    rows.pop(1)
    return 1


def reenter_default(value):
    global rows
    # The encoder must retain the current value and pending rows when external
    # references disappear across nested encoding and allocating callbacks.
    rows = []
    garbage = []
    for i in range(30):
        garbage.append(InheritedRow((i, [i])))
    assert active_encoder.dumps(
        mapping([InheritedRow(("nested", 7))])) == '{"nested": 7}'
    assert value.marker == 9
    return value.marker


class ReenterSortRow(InheritedRow):
    def __lt__(self, other):
        global rows
        # list.sort temporarily hides its items; nested encoding and allocation
        # must preserve both the sort state and the tuple comparison operands.
        assert rows == []
        rows = []
        garbage = []
        for i in range(30):
            garbage.append(InheritedRow((i, [i])))
        assert active_encoder.dumps(
            mapping([InheritedRow(("nested", 7))])) == '{"nested": 7}'
        return tuple.__lt__(self, other)


def check_tuple_rows(encoder):
    global rows, calls, active_encoder
    active_encoder = encoder
    for row_type in (RaisingRow, InheritedRow, MisleadingRow,
                     MisleadingLengthRow):
        for sorting in (False, True):
            rows = [row_type(("z", 1)), row_type(("a", 2))]
            expected = '{"a": 2, "z": 1}' if sorting else '{"z": 1, "a": 2}'
            assert encoder.dumps(mapping(rows), sort_keys=sorting) == expected
            assert tuple.__getitem__(rows[0], 0) == ("a" if sorting else "z")

            for payload in ((), ("a",), ("a", 1, 2)):
                try:
                    encoder.dumps(mapping([row_type(payload)]), sort_keys=sorting)
                except ValueError as error:
                    assert str(error) == "items must return 2-tuples"
                else:
                    assert False, "malformed stored length accepted"

    for sorting in (False, True):
        calls = []
        rows = [InheritedRow(("a", Unsupported())), InheritedRow(("z",))]
        try:
            encoder.dumps(mapping(rows), default=callback, sort_keys=sorting)
        except ValueError as error:
            assert str(error) == "items must return 2-tuples"
        else:
            assert False, "later malformed row accepted"
        assert calls == ["default"]

        rows = [InheritedRow(("a", Unsupported())), InheritedRow(("b", 2))]
        assert encoder.dumps(
            mapping(rows), default=append_row,
            sort_keys=sorting) == '{"a": 1, "b": 2, "c": 3}'
        assert len(rows) == 3
        rows = [InheritedRow(("a", Unsupported())), InheritedRow(("b", 2))]
        assert encoder.dumps(
            mapping(rows), default=delete_row, sort_keys=sorting) == '{"a": 1}'
        assert len(rows) == 1

        value = Unsupported()
        value.marker = 9
        rows = [InheritedRow(("a", value)), InheritedRow(("b", [2, 3]))]
        value = None
        assert encoder.dumps(
            mapping(rows), default=reenter_default,
            sort_keys=sorting) == '{"a": 9, "b": [2, 3]}'
        assert rows == []

    rows = [SortRow(()), SortRow(())]
    try:
        encoder.dumps(mapping(rows), sort_keys=True)
    except ComparisonError as error:
        assert str(error) == "sorting first"
    else:
        assert False, "row validation preceded sorting"
    assert len(rows) == 2

    rows = [ReverseRow(("a", 1)), ReverseRow(("z", 2))]
    assert encoder.dumps(
        mapping(rows), sort_keys=True) == '{"z": 2, "a": 1}'
    assert tuple.__getitem__(rows[0], 0) == "z"
    rows = [ReenterSortRow(("z", 2)), ReenterSortRow(("a", 1))]
    assert encoder.dumps(
        mapping(rows), sort_keys=True) == '{"a": 1, "z": 2}'
    assert rows == []
    assert encoder.dumps(
        mapping([InheritedRow(("still", "works"))])) == '{"still": "works"}'


check_tuple_rows(json)
