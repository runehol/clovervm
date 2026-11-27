"""Compare the managed Excel slice with CPython's csv (run with python3.14)."""

import csv
import importlib.util
from itertools import product
from pathlib import Path


root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("clover_csv", root / "stdlib/csv.py")
managed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(managed)


def read_events(module, lines):
    reader = module.reader(lines)
    events = []
    while True:
        try:
            row = next(reader)
        except StopIteration:
            events.append(("eof", reader.line_num))
            return events
        except module.Error:
            events.append(("error", reader.line_num))
        else:
            events.append((row, reader.line_num))


class Sink:
    def __init__(self):
        self.chunks = []

    def write(self, text):
        self.chunks.append(text)
        return len(text)


def write_record(module, row):
    sink = Sink()
    result = module.writer(sink).writerow(row)
    return sink.chunks, result


for length in range(5):
    for chars in product('a,"\r\n\x00', repeat=length):
        text = "".join(chars)
        for cut in range(len(text) + 1):
            lines = [text[:cut], text[cut:], "next"]
            assert read_events(managed, lines) == read_events(csv, lines), lines

for length in range(4):
    for row in product(("", "a", " a ", ",", '"', "\r", "\n", "\0\u00e9", None, 42),
                       repeat=length):
        assert write_record(managed, row) == write_record(csv, row), row

limit = csv.field_size_limit()
lines = ["\u00e9" * limit, "\u00e9" * (limit + 1), "next"]
assert read_events(managed, lines) == read_events(csv, lines)
print("Excel CSV differential passed")
