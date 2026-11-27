"""Read and write CSV using the default Excel dialect."""

__all__ = ["Error", "reader", "writer"]

_FIELD_LIMIT = 131072

_START_RECORD = 0
_START_FIELD = 1
_UNQUOTED = 2
_QUOTED = 3
_AFTER_QUOTE = 4
_AFTER_NEWLINE = 5


class Error(Exception):
    pass


def _check_dialect(dialect):
    if dialect is None:
        return
    if not isinstance(dialect, str):
        raise TypeError("dialect must be 'excel' or None")
    if str.__str__(dialect) != "excel":
        raise Error("unknown dialect")


class _Reader:
    def __init__(self, source):
        self._source = source
        self._line_num = 0

    @property
    def line_num(self):
        return self._line_num

    def __iter__(self):
        return self

    def __next__(self):
        row = []
        field = []
        state = _START_RECORD
        while True:
            try:
                line = next(self._source)
            except StopIteration:
                if state == _QUOTED:
                    row.append("".join(field))
                    return row
                raise

            if not isinstance(line, str):
                raise Error("iterator should return strings (open the file in text mode)")
            self._line_num += 1
            line = str.__str__(line)

            if state == _START_RECORD and '"' not in line:
                body = line.rstrip("\r\n")
                # A short, unquoted record needs no per-character scan. A
                # longer record may still contain only fields within the limit.
                if len(body) <= _FIELD_LIMIT and "\r" not in body and "\n" not in body:
                    if not body:
                        return row
                    return body.split(",")

            for char in line:
                if state == _AFTER_NEWLINE:
                    if char != "\r" and char != "\n":
                        raise Error("new-line character seen in unquoted field (use newline='')")
                    continue

                if state == _QUOTED:
                    if char == '"':
                        state = _AFTER_QUOTE
                        continue
                elif state == _AFTER_QUOTE and char == '"':
                    state = _QUOTED
                elif char == "\r" or char == "\n":
                    if state != _START_RECORD:
                        row.append("".join(field))
                    state = _AFTER_NEWLINE
                    continue
                elif char == ",":
                    row.append("".join(field))
                    field = []
                    state = _START_FIELD
                    continue
                elif char == '"' and (state == _START_RECORD or state == _START_FIELD):
                    state = _QUOTED
                    continue
                else:
                    state = _UNQUOTED

                if len(field) == _FIELD_LIMIT:
                    raise Error("field larger than field limit (131072)")
                field.append(char)

            # A source boundary ends a record unless it falls inside quotes.
            # Only characters present in the source become part of the field.
            if state == _QUOTED:
                continue
            if state != _START_RECORD and state != _AFTER_NEWLINE:
                row.append("".join(field))
            return row


def reader(source, /, dialect="excel"):
    """Return an iterator over the CSV records in an iterable of text lines."""
    lines = iter(source)
    _check_dialect(dialect)
    return _Reader(lines)


class _Writer:
    def __init__(self, write):
        self._write = write

    def writerow(self, row, /):
        """Write one record and return the result of the stream's write call."""
        try:
            fields = iter(row)
        except TypeError:
            raise Error("iterable expected")

        encoded = []
        while True:
            try:
                field = next(fields)
            except StopIteration:
                break
            if field is None:
                text = ""
            elif isinstance(field, str):
                text = str.__str__(field)
            else:
                text = str.__str__(str(field))

            if '"' in text:
                text = '"' + text.replace('"', '""') + '"'
            elif "," in text or "\r" in text or "\n" in text:
                text = '"' + text + '"'
            encoded.append(text)

        if len(encoded) == 1 and encoded[0] == "":
            encoded[0] = '""'
        return self._write(",".join(encoded) + "\r\n")

    def writerows(self, rows, /):
        """Write each record from an iterable of rows."""
        for row in rows:
            _Writer.writerow(self, row)


def writer(stream, /, dialect="excel"):
    """Return a CSV writer for a stream with a callable write method."""
    try:
        write = stream.write
    except AttributeError:
        raise TypeError('argument 1 must have a "write" method')
    if not callable(write):
        raise TypeError('argument 1 must have a "write" method')
    _check_dialect(dialect)
    return _Writer(write)
