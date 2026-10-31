# Original regressions checked against CPython 3.14.10. Exercise both search
# directions, preprocessing reuse, overlap selection, and exact result policies.

# Construct repeated fixtures using the existing bytearray operations.
def repeat_binary(data, count):
    result = bytearray()
    block = bytearray(data)
    while count:
        if count % 2:
            result.extend(block)
        count = count // 2
        if count:
            block.extend(block)
    return bytes(result)


def check_long_separators():
    for width in (2, 3, 4, 5, 8, 9, 16, 17, 31, 32, 33, 64, 65, 127, 128):
        for separator in (repeat_binary(b'a', width), (repeat_binary(b'ab', width))[:width],
                          repeat_binary(b'a', width - 1) + b'b',
                          b'b' + repeat_binary(b'a', width - 1),
                          (repeat_binary(b'\x00\xff\x80', width))[:width]):
            data = b'left' + separator + b'middle' + separator + b'right'
            for mutable in (False, True):
                source = bytearray(data) if mutable else data
                for sep in (separator, bytearray(separator)):
                    assert source.split(sep) == [b'left', b'middle', b'right']
                    assert source.rsplit(sep) == [b'left', b'middle', b'right']
                    assert source.split(sep, 1) == [b'left', b'middle' + separator + b'right']
                    assert source.rsplit(sep, 1) == [b'left' + separator + b'middle', b'right']
                    assert source.split(sep, 0) == [data]
                    assert source.rsplit(sep, 0) == [data]
                    first = source.partition(sep)
                    last = source.rpartition(sep)
                    assert first == (b'left', separator, b'middle' + separator + b'right')
                    assert last == (b'left' + separator + b'middle', separator, b'right')
                    if mutable:
                        for parts in (source.split(sep), source.rsplit(sep), first, last):
                            for part in parts:
                                assert part.__class__ is bytearray
                                assert part is not source and part is not sep
                        saved = bytes(first[1])
                        first[1].append(33)
                        assert source == data and sep == separator
                        assert last[1] == saved
                    else:
                        assert first[1] is sep and last[1] is sep


def check_periodic_overlaps():
    for width in (2, 4, 8, 16, 32, 64):
        sep = repeat_binary(b'ab', width) + b'a'
        data = sep + b'ba'
        for mutable in (False, True):
            source = bytearray(data) if mutable else data
            assert source.split(sep) == [b'', b'ba']
            assert source.rsplit(sep) == [b'ab', b'']
            assert source.partition(sep) == (b'', sep, b'ba')
            assert source.rpartition(sep) == (b'ab', sep, b'')
            same = bytearray(data) if mutable else data
            assert source.split(same) == [b'', b'']
            assert source.rsplit(same) == [b'', b'']
            assert source.split(source) == [b'', b'']
            assert source.rsplit(source) == [b'', b'']
            assert source.partition(source) == (b'', data, b'')
            assert source.rpartition(source) == (b'', data, b'')


def check_near_misses():
    for width in (64, 4096, 32768):
        data = repeat_binary(b'a', width * 2)
        for separator in (repeat_binary(b'a', width) + b'b', b'b' + repeat_binary(b'a', width)):
            for mutable in (False, True):
                source = bytearray(data) if mutable else data
                assert source.split(separator) == [data]
                assert source.rsplit(separator) == [data]
                assert source.partition(separator) == (data, b'', b'')
                assert source.rpartition(separator) == (b'', b'', data)
                if mutable:
                    for part in (source.split(separator)[0],
                                 source.rsplit(separator)[0],
                                 source.partition(separator)[0],
                                 source.rpartition(separator)[2]):
                        assert part is not source
                        part[0] = 98
                        assert source == data
                else:
                    assert source.split(separator)[0] is source
                    assert source.rsplit(separator)[0] is source
                    assert source.partition(separator)[0] is source
                    assert source.rpartition(separator)[2] is source


def check_mutable_separator_changes():
    for mutable in (False, True):
        separator = bytearray(repeat_binary(b'a', 128) + b'b')
        data = b'left' + bytes(separator) + b'right'
        source = bytearray(data) if mutable else data
        for repeat in range(4):
            assert source.split(separator) == [b'left', b'right']
            assert source.rpartition(separator) == (b'left', separator, b'right')
            separator[-1] = 99
            assert source.split(separator) == [data]
            assert source.rpartition(separator) == (b'', b'', data)
            separator[-1] = 98


check_long_separators()
check_periodic_overlaps()
check_near_misses()

check_mutable_separator_changes()
