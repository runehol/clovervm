import csv


class Sink:
    def __init__(self):
        self.chunks = []
        self.result = object()

    def write(self, text):
        self.chunks.append(text)
        return self.result


sink = Sink()
writer = csv.writer(sink)
assert writer.writerow([]) is sink.result
assert writer.writerows([[""], ["x,y", 'a"b', "one\r\ntwo", None, 42]]) is None
assert sink.chunks == ["\r\n", '""\r\n', '"x,y","a""b","one\r\ntwo",,42\r\n']

reader = csv.reader("".join(sink.chunks).splitlines(True), dialect="excel")
assert iter(reader) is reader
assert reader.line_num == 0
assert next(reader) == [] and reader.line_num == 1
assert next(reader) == [""] and reader.line_num == 2
assert next(reader) == ["x,y", 'a"b', "one\r\ntwo", "", "42"]
assert reader.line_num == 4
assert next(reader, None) is None and next(reader, None) is None
try:
    reader.line_num = 0
    assert False
except AttributeError:
    pass

assert list(csv.reader(["", "a\r", "b\n", "c\r\n\r", ",", ",x,", 'a"b', ' "x"'])) == [
    [], ["a"], ["b"], ["c"], ["", ""], ["", "x", ""], ['a"b'], [' "x"'],
]
assert list(csv.reader(['"x', "", 'y"z,"a""b"'], "excel")) == [["xyz", 'a"b']]
assert list(csv.reader(['a,"'], None)) == [["a", ""]]
assert list(csv.reader(['"\x00\u00e9\n'])) == [["\x00\u00e9\n"]]

reader = csv.reader([b"not text", "a\nb", "ok"])
for line_num in (0, 1):
    try:
        next(reader)
        assert False
    except csv.Error:
        assert reader.line_num == line_num
assert next(reader) == ["ok"] and reader.line_num == 2


class Source:
    def __init__(self, items, error):
        self.items = items
        self.error = error
        self.acquisitions = 0
        self.index = 0

    def __iter__(self):
        self.acquisitions += 1
        return self

    def __next__(self):
        if self.index == len(self.items):
            raise StopIteration
        value = self.items[self.index]
        self.index += 1
        if value is self.error:
            raise self.error
        return value


error = ValueError("source error")
source = Source(['"partial\n', error, "fresh", "", "last"], error)
reader = csv.reader(source)
assert source.acquisitions == 1 and source.index == 0
try:
    next(reader)
    assert False
except ValueError as caught:
    assert caught is error
assert reader.line_num == 1 and source.index == 2
assert next(reader) == ["fresh"] and reader.line_num == 2
assert list(reader) == [[], ["last"]] and source.acquisitions == 1


class Text(str):
    def __iter__(self):
        raise AssertionError

    def __str__(self):
        raise AssertionError

    def __contains__(self, value):
        raise AssertionError

    def replace(self, old, new):
        raise AssertionError

    def rstrip(self, chars):
        raise AssertionError

    def split(self, sep):
        raise AssertionError


assert list(csv.reader([Text("a,b,\r\n")])) == [["a", "b", ""]]
sink = Sink()
writer = csv.writer(sink)


def replacement(self, text):
    raise AssertionError


Sink.write = replacement
assert writer.writerow([Text('a"b'), None, True, 1.5, b"x"]) is sink.result
assert sink.chunks == ['"a""b",,True,1.5,b\'x\'\r\n']
writer.writerow([None])
writer.writerow(["", None])
writer.writerow(["\r", "\n", "\v", "\x00"])
assert sink.chunks[1:] == ['""\r\n', ",\r\n", '"\r","\n",\v,\x00\r\n']


class Converted:
    def __init__(self, result):
        self.result = result
        self.calls = 0

    def __str__(self):
        self.calls += 1
        if isinstance(self.result, Exception):
            raise self.result
        return self.result


field = Converted(Text("x,y"))
row = Source(["first", field], error)
writer.writerow(row)
assert field.calls == 1 and row.acquisitions == 1
assert sink.chunks[-1] == 'first,"x,y"\r\n'
bad_row = Source(["partial", error], error)
try:
    writer.writerows([["before"], bad_row, ["after"]])
    assert False
except ValueError as caught:
    assert caught is error
assert sink.chunks[-1] == "before\r\n"

failure = StopIteration("conversion failed")
try:
    writer.writerows([[Converted(failure)], ["after"]])
    assert False
except StopIteration as caught:
    assert caught is failure
assert sink.chunks[-1] == "before\r\n"


class FailingSink:
    def write(self, text):
        raise error


remaining = Source([["before"], ["after"]], error)
try:
    csv.writer(FailingSink()).writerows(remaining)
    assert False
except ValueError as caught:
    assert caught is error
assert remaining.index == 1

try:
    writer.writerow(42)
    assert False
except csv.Error:
    pass


class NonCallableSink:
    write = None


for invalid_sink in (object(), NonCallableSink()):
    try:
        csv.writer(invalid_sink)
        assert False
    except TypeError:
        pass
