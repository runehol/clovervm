# Independent fixtures checked against CPython 3.14.10.
# No CPython test code is copied. Unicode is escaped for the C++ source harness.

class PaddingString(str):
    def __len__(self):
        raise AssertionError("stored length required")
    def __getitem__(self, key):
        raise AssertionError("stored payload required")
    def __str__(self):
        raise AssertionError("stored payload required")

class PaddingInt(int):
    def __index__(self):
        raise AssertionError("stored integer required")
    def __int__(self):
        raise AssertionError("stored integer required")


def check_padding():
    assert 'abc'.ljust(6) == 'abc   '
    assert 'abc'.rjust(6) == '   abc'
    assert 'abc'.center(6) == ' abc  '
    assert 'ab'.center(5, '-') == '--ab-'
    assert 'abc'.center(4, '-') == 'abc-'
    assert 'abc'.center(5, '-') == '-abc-'
    assert 'abc'.center(6, '-') == '-abc--'
    assert 'abc'.center(7, '-') == '--abc--'
    for method in (str.ljust, str.rjust, str.center):
        assert method('', 3) == '   '
        assert method('', 3, '\U0001f600') == '\U0001f600\U0001f600\U0001f600'
        assert method('a', True, 'x') == 'a'
        assert method('', True, 'x') == 'x'
        for text in ('', 'abc', '\xe9', '\U0001f600', 'a\x00b', 'e\u0301'):
            for width in (-9223372036854775808, PaddingInt(-2**60), -2, 0, False):
                assert method(text, width) is text
                result = method(PaddingString(text), width)
                assert result == text
                assert result.__class__ is str
            result = method(PaddingString(text), PaddingInt(8), PaddingString('\U0001f600'))
            assert result.__class__ is str
            assert len(result) == 8
        text = 'abc'
        assert method(text, 3) is text
    assert '\U0001f600'.ljust(3, '\xe9') == '\U0001f600\xe9\xe9'
    assert '\U0001f600'.rjust(3, '\xe9') == '\xe9\xe9\U0001f600'
    assert 'e\u0301'.center(4, '\x00') == '\x00e\u0301\x00'
    assert '\x00'.ljust(3, '\x00') == '\x00\x00\x00'
    # Build no expected values with sequence multiplication (separate scope).
    result = 'abc'.ljust(1024, 'x')
    assert len(result) == 1024
    assert result[:3] == 'abc'
    for index in range(3, 1024):
        assert result[index] == 'x'


def check_zfill():
    for case in (('', '00000'), ('1', '00001'), ('+1', '+0001'),
                 ('-1', '-0001'), ('+-1', '+00-1'), ('-', '-0000'),
                 ('\u22121', '000\u22121'), ('\uff0b1', '000\uff0b1'),
                 ('\U0001f600', '0000\U0001f600'), ('\x00', '0000\x00')):
        assert case[0].zfill(5) == case[1]
        result = PaddingString(case[0]).zfill(PaddingInt(5))
        assert result == case[1]
        assert result.__class__ is str
    for text in ('', 'abc', '+1', '\U0001f600'):
        for width in (-9223372036854775808, PaddingInt(-2**60), -1, 0, False, len(text)):
            assert text.zfill(width) is text
            result = PaddingString(text).zfill(width)
            assert result == text
            assert result.__class__ is str
    assert ''.zfill(True) == '0'


def check_errors():
    for method in (str.ljust, str.rjust, str.center, str.zfill):
        for width in (None, '3', 3.0, []):
            raised = False
            try:
                method('abc', width)
            except TypeError:
                raised = True
            assert raised
        for width in (9223372036854775808, -9223372036854775809, 10**100, -10**100, PaddingInt(10**100)):
            raised = False
            try:
                method('abc', width)
            except OverflowError:
                raised = True
            assert raised
        for args in ((), ('abc',), (1, 2), ('abc', 3, 'x', 'extra')):
            raised = False
            try:
                method(*args)
            except TypeError:
                raised = True
            assert raised
        raised = False
        try:
            method('abc', width=4)
        except TypeError:
            raised = True
        assert raised
    for method in (str.ljust, str.rjust, str.center):
        for fill in (None, 1, b'x', '', 'xx', 'e\u0301', PaddingString('')):
            for width in (-1, 0, 10):
                raised = False
                try:
                    method('abc', width, fill)
                except TypeError:
                    raised = True
                assert raised
            raised = False
            try:
                method('abc', 10**100, fill)
            except OverflowError:
                raised = True
            assert raised
        raised = False
        try:
            method('abc', 4, fillchar='x')
        except TypeError:
            raised = True
        assert raised
    raised = False
    try:
        'abc'.zfill(4, 'x')
    except TypeError:
        raised = True
    assert raised


class PaddingOverride(str):
    def center(self, width, fill=' '):
        return 'override'


def cached_padding(text, fill):
    return text.center(5, fill)


def check_calls():
    for iteration in range(32):
        assert cached_padding('a', '-') == '--a--'
        assert cached_padding(PaddingString('a'), PaddingString('\U0001f600')) == '\U0001f600\U0001f600a\U0001f600\U0001f600'
        assert cached_padding(PaddingOverride('a'), '-') == 'override'
        # A fill error must not be hidden by a warmed method-call cache.
        raised = False
        try:
            cached_padding('abcdef', '')
        except TypeError:
            raised = True
        assert raised


check_padding()
check_zfill()
check_errors()
check_calls()
