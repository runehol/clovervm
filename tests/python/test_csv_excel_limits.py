import csv


class Sink:
    def __init__(self):
        self.chunks = []

    def write(self, text):
        self.chunks.append(text)


sink = Sink()


def check_dialect_boundary(factory, arg):
    try:
        factory(arg, "unix")
        assert False
    except csv.Error:
        pass
    try:
        factory(arg, object())
        assert False
    except TypeError:
        pass
    try:
        factory(arg, delimiter=";")
        assert False
    except TypeError:
        pass


check_dialect_boundary(csv.reader, [])
check_dialect_boundary(csv.writer, sink)
writer = csv.writer(sink, dialect=None)


class NestedWrite:
    def __str__(self):
        writer.writerow(["inner"])
        return "converted"


writer.writerow(["outer", NestedWrite(), "tail"])
assert sink.chunks == ["inner\r\n", "outer,converted,tail\r\n"]


class NestedRead:
    def __init__(self):
        self.calls = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.calls += 1
        if self.calls == 1:
            return '"outer '
        if self.calls == 2:
            assert next(self.reader) == ["inner"]
            assert self.reader.line_num == 2
            return 'tail"'
        if self.calls == 3:
            return "inner"
        raise StopIteration


source = NestedRead()
source.reader = csv.reader(source)
assert next(source.reader) == ["outer tail"]
assert source.reader.line_num == 3
assert next(source.reader, None) is None

field = "\u00e9"
for _ in range(17):
    field += field
reader = csv.reader([field + "\r\n", field + ",", field + "x", "recovered"])
assert next(reader) == [field]
assert next(reader) == [field, ""]
try:
    next(reader)
    assert False
except csv.Error:
    assert reader.line_num == 3
assert next(reader) == ["recovered"] and reader.line_num == 4
