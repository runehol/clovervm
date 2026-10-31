# Original regressions checked against CPython 3.14.10. Only explicit-separator
# search is shared with bytes; whitespace and argument policy remain separate.

def repeat_search_text(text, count):
    result = ''
    block = text
    while count:
        if count % 2:
            result = result + block
        count = count // 2
        if count:
            block = block + block
    return result


def check_wide_separators():
    for width in (1, 2, 7, 8, 9, 16, 17, 31, 64, 129):
        for sep in (repeat_search_text('a', width),
                    repeat_search_text('\u0100\u0200\U00010000', width)[:width],
                    repeat_search_text('a', width - 1) + 'b',
                    'b' + repeat_search_text('a', width - 1),
                    repeat_search_text('\x00\ud800\U0010ffff', width)[:width]):
            source = 'left' + sep + 'middle' + sep + 'right'
            assert source.split(sep) == ['left', 'middle', 'right']
            assert source.rsplit(sep) == ['left', 'middle', 'right']
            assert source.split(sep, 1) == ['left', 'middle' + sep + 'right']
            assert source.rsplit(sep, 1) == ['left' + sep + 'middle', 'right']
            assert source.split(sep, 0) == [source]
            assert source.rsplit(sep, 0) == [source]
            assert source.partition(sep) == ('left', sep, 'middle' + sep + 'right')
            assert source.rpartition(sep) == ('left' + sep + 'middle', sep, 'right')
            assert ''.split(sep) == ['']
            assert ''.rsplit(sep) == ['']
            assert source.split(source) == ['', '']
            assert source.rsplit(source) == ['', '']
            assert source.partition(source) == ('', source, '')
            assert source.rpartition(source) == ('', source, '')


def check_overlaps_and_width():
    for width in (4, 8, 16, 64):
        sep = repeat_search_text('\u0100\u0200', width) + '\u0100'
        source = sep + '\u0200\u0100'
        assert source.split(sep) == ['', '\u0200\u0100']
        assert source.rsplit(sep) == ['\u0100\u0200', '']
        assert source.partition(sep) == ('', sep, '\u0200\u0100')
        assert source.rpartition(sep) == ('\u0100\u0200', sep, '')
        missing = repeat_search_text('\u0300\u0400', width) + '\u0300'
        assert source.split(missing) == [source]
        assert source.rsplit(missing) == [source]
        assert source.partition(missing) == (source, '', '')
        assert source.rpartition(missing) == ('', '', source)
    for n in (128, 4096, 32768):
        source = repeat_search_text('\U00010000', n)
        for sep in (repeat_search_text('\U00010000', n // 2) + '\U00020000',
                    '\U00020000' + repeat_search_text('\U00010000', n // 2)):
            assert source.split(sep) == [source]
            assert source.rsplit(sep) == [source]
            assert source.partition(sep) == (source, '', '')
            assert source.rpartition(sep) == ('', '', source)


def check_many_pieces():
    # Cover offset storage both below and above its inline capacity.
    for count in (7, 8, 9, 64):
        for sep in (':', repeat_search_text('\u0100\U00010000', 9)):
            source = repeat_search_text('field' + sep, count) + 'last'
            for parts in (source.split(sep), source.rsplit(sep)):
                assert len(parts) == count + 1
                for index in range(count):
                    assert parts[index] == 'field'
                assert parts[-1] == 'last'


class SearchString(str):
    def __len__(self):
        raise AssertionError
    def __getitem__(self, key):
        raise AssertionError
    def __str__(self):
        raise AssertionError


def check_subclass_and_errors():
    sep = SearchString(repeat_search_text('\u0100\U00010000', 16))
    source = SearchString('left' + sep + 'right')
    for method in (str.split, str.rsplit):
        result = method(source, sep)
        assert result == ['left', 'right']
        for part in result:
            assert part.__class__ is str
        assert method(source, sep=sep, maxsplit=1) == ['left', 'right']
        raised = False
        try:
            method(source, '', 0)
        except ValueError:
            raised = True
        assert raised
    for method in (str.partition, str.rpartition):
        result = method(source, sep)
        assert result == ('left', sep, 'right')
        assert result[0].__class__ is str and result[2].__class__ is str
        assert result[1] is sep
        missing = method(source, 'missing')
        assert missing[0 if method is str.partition else 2] is source
    # Repeated cached calls with distinct wide alphabets and pattern lengths.
    for index in range(110):
        assert source.split(sep) == ['left', 'right']
        assert '\u0100:\U00010000'.rsplit(':', 1) == ['\u0100', '\U00010000']


check_wide_separators()
check_overlaps_and_width()
check_many_pieces()
check_subclass_and_errors()
