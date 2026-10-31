# Independent semantic regressions, checked against CPython 3.14.10.
def check_lines():
    for cls in (str, bytes, bytearray):
        empty = cls()
        assert empty.splitlines() == []
        if cls is str:
            text = 'a\r\nb\n\rc\r'
            expected = ['a', 'b', '', 'c']
            retained = ['a\r\n', 'b\n', '\r', 'c\r']
        else:
            text = cls(b'a\r\nb\n\rc\r')
            expected = [cls(b'a'), cls(b'b'), cls(), cls(b'c')]
            retained = [cls(b'a\r\n'), cls(b'b\n'), cls(b'\r'), cls(b'c\r')]
        assert text.splitlines() == expected
        assert text.splitlines(keepends=False) == expected
        assert text.splitlines(keepends=True) == retained
        assert text.splitlines(1) == retained
        assert text.splitlines(None) == expected
    for cp in (10, 11, 12, 13, 28, 29, 30, 133, 8232, 8233):
        boundary = chr(cp)
        assert ('a' + boundary + 'b').splitlines() == ['a', 'b']
        assert (boundary + boundary).splitlines() == ['', '']
        assert (boundary + boundary).splitlines(True) == [boundary, boundary]
    for cp in (0, 9, 31, 127, 160, 128512):
        text = 'a' + chr(cp) + 'b'
        assert text.splitlines() == [text]
    for cls in (bytes, bytearray):
        for cp in (0, 9, 11, 12, 28, 29, 30, 133, 255):
            text = cls([97, cp, 98])
            assert text.splitlines() == [text]
    assert '\U0001f600\u2028e\u0301\r\n'.splitlines() == ['\U0001f600', 'e\u0301']


def check_tabs():
    for cls in (str, bytes, bytearray):
        text = 'ab\tX\r\nq\tZ\t' if cls is str else cls(b'ab\tX\r\nq\tZ\t')
        expected = 'ab  X\r\nq   Z   ' if cls is str else cls(b'ab  X\r\nq   Z   ')
        assert text.expandtabs(tabsize=4) == expected
        assert text.expandtabs() == text.expandtabs(8)
        plain = 'abX\r\nqZ' if cls is str else cls(b'abX\r\nqZ')
        for size in (0, -1, -2147483648, False):
            assert text.expandtabs(size) == plain
        assert cls().expandtabs(2147483647) == cls()
        empty = cls()
        expanded = empty.expandtabs()
        assert expanded.__class__ is cls
        if cls is bytearray:
            assert expanded is not empty
        else:
            assert expanded is empty
    assert '\U0001f600\tX'.expandtabs(4) == '\U0001f600   X'
    assert 'e\u0301\tX'.expandtabs(4) == 'e\u0301  X'
    for cp in (11, 12, 28, 29, 30, 133, 8232, 8233):
        assert ('ab' + chr(cp) + '\tX').expandtabs(4) == 'ab' + chr(cp) + ' X'
    assert 'a\x00\t'.expandtabs(4) == 'a\x00  '
    assert b'\xff\t'.expandtabs(4) == b'\xff   '


class Text(str):
    def __len__(self):
        raise AssertionError('stored length required')
    def __getitem__(self, index):
        raise AssertionError('stored payload required')
class Binary(bytes):
    def __len__(self):
        raise AssertionError('stored length required')
    def __getitem__(self, index):
        raise AssertionError('stored payload required')
# bytearray subclassing is an existing excluded VM surface.
class Int(int):
    def __index__(self):
        raise AssertionError('stored integer required')


def check_types():
    for row in ((str, Text, 'ab'), (bytes, Binary, b'ab'),
                (bytearray, bytearray, b'ab')):
        cls = row[0]
        sub = row[1]
        value = row[2]
        original = cls(value)
        child = sub(value)
        for text in (original, child):
            result = text.expandtabs(Int(4))
            assert result == original
            assert result.__class__ is cls
            parts = text.splitlines()
            assert parts == [original]
            assert parts[0].__class__ is cls
        if cls is bytearray:
            assert original.splitlines()[0] is not original
        else:
            assert original.splitlines()[0] is original
        assert child.splitlines()[0] is not child
        assert child.expandtabs() is not child
        if cls is str:
            assert original.expandtabs() is original
        else:
            assert original.expandtabs() is not original
        for value in (2147483648, -2147483649, 10**100, -10**100):
            try:
                cls().expandtabs(value)
            except OverflowError:
                pass
            else:
                raise AssertionError('C int conversion required even on empty input')
        for value in (None, 1.0, '4'):
            try:
                original.expandtabs(value)
            except TypeError:
                pass
            else:
                raise AssertionError('integer expected')


callback_events = []
callback_failure = ValueError('flag failure')
mutable_source = bytearray(b'a')


def check_callbacks():
    class Flag:
        def __bool__(self):
            callback_events.append('bool')
            # Reentry and allocation happen before any native payload borrowing.
            assert 'a\nb'.splitlines(True) == ['a\n', 'b']
            assert '\t'.expandtabs(2) == '  '
            return True
    class Length:
        def __len__(self):
            callback_events.append('len')
            return 0
    class Broken:
        def __bool__(self):
            raise callback_failure
    class Invalid:
        def __bool__(self):
            return 1
    for cls in (str, bytes, bytearray):
        for text in (cls(), cls('a\nb') if cls is str else cls(b'a\nb')):
            before = len(callback_events)
            assert text.splitlines(Flag()) == text.splitlines(True)
            assert callback_events[before:] == ['bool']
            assert text.splitlines(Length()) == text.splitlines(False)
            try:
                text.splitlines(Broken())
            except ValueError as caught:
                assert caught is callback_failure
            else:
                raise AssertionError('lost callback exception')
            try:
                text.splitlines(Invalid())
            except TypeError:
                pass
            else:
                raise AssertionError('invalid bool result')
        before = len(callback_events)
        try:
            cls.splitlines(123, Flag())
        except TypeError:
            pass
        else:
            raise AssertionError('invalid receiver')
        assert len(callback_events) == before
        for case in (0, 1, 2):
            try:
                if case == 0:
                    cls.splitlines(cls(), Flag(), keepends=False)
                elif case == 1:
                    cls.splitlines(cls(), other=Flag())
                else:
                    cls.splitlines(cls(), Flag(), False)
            except TypeError:
                pass
            else:
                raise AssertionError('bad signature')
        assert len(callback_events) == before
    class Mutating:
        def __bool__(self):
            mutable_source.extend(b'\nb')
            return False
    assert mutable_source.splitlines(Mutating()) == [bytearray(b'a'), bytearray(b'b')]
    pieces = mutable_source.splitlines()
    mutable_source[0] = 120
    assert pieces == [bytearray(b'a'), bytearray(b'b')]


check_lines()
check_tabs()
check_types()
check_callbacks()


def check_signatures_and_hot_calls():
    for cls in (str, bytes, bytearray):
        text = cls()
        for case in (0, 1, 2, 3, 4):
            try:
                if case == 0:
                    text.expandtabs(4, tabsize=4)
                elif case == 1:
                    text.expandtabs(other=4)
                elif case == 2:
                    text.expandtabs(4, 4)
                elif case == 3:
                    cls.expandtabs(123, None)
                else:
                    cls.splitlines(self=text)
            except TypeError:
                pass
            else:
                raise AssertionError('bad call accepted')
    class Hot:
        def __bool__(self):
            return True
    flag = Hot()
    for i in range(100):
        assert 'a\nb'.splitlines(flag) == ['a\n', 'b']
        assert b'a\nb'.splitlines(flag) == [b'a\n', b'b']
        assert bytearray(b'a\nb').splitlines(flag) == [bytearray(b'a\n'), bytearray(b'b')]
    def false(self):
        return False
    Hot.__bool__ = false
    assert 'a\nb'.splitlines(flag) == ['a', 'b']
    assert b'a\nb'.splitlines(flag) == [b'a', b'b']
    assert bytearray(b'a\nb').splitlines(flag) == [bytearray(b'a'), bytearray(b'b')]


check_signatures_and_hot_calls()
