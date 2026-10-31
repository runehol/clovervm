# Independent fixtures checked against CPython 3.14.10; no CPython test code copied.
class PaddingBytes(bytes):
    def __len__(self):
        raise AssertionError('stored length required')
    def __getitem__(self, key):
        raise AssertionError('stored payload required')
    def __bytes__(self):
        raise AssertionError('stored payload required')

class PaddingWidth(int):
    def __index__(self):
        raise AssertionError('stored width required')
    def __int__(self):
        raise AssertionError('stored width required')


def expect_type_error(method, *args):
    raised = False
    try:
        method(*args)
    except TypeError:
        raised = True
    assert raised


def check_values():
    for cls in (bytes, bytearray):
        assert cls(b'abc').ljust(6) == cls(b'abc   ')
        assert cls(b'abc').rjust(6) == cls(b'   abc')
        assert cls(b'ab').center(5, b'-') == cls(b'--ab-')
        assert cls(b'abc').center(6, b'-') == cls(b'-abc--')
        assert cls(b'abc').center(4, b'-') == cls(b'abc-')
        assert cls(b'abc').center(5, b'-') == cls(b'-abc-')
        assert cls(b'abc').center(7, b'-') == cls(b'--abc--')
        for name in ('ljust', 'rjust', 'center', 'zfill'):
            method = getattr(cls, name)
            for data in (b'', b'abc', b'\x00\xff', b'+1', b'-1'):
                source = cls(data)
                for width in (-9223372036854775808, PaddingWidth(-2**60), -2, 0,
                              False, len(source)):
                    result = method(source, width)
                    assert result == source
                    assert result.__class__ is cls
                    if cls is bytes:
                        assert result is source
                    else:
                        assert result is not source
                        result.append(42)
                        assert source == cls(data)
                result = method(source, PaddingWidth(9))
                assert result.__class__ is cls
                assert len(result) == 9
            assert method(cls(b''), True) == cls(b'0' if name == 'zfill' else b' ')
        for name in ('ljust', 'rjust', 'center'):
            method = getattr(cls, name)
            for case in ((b'\x00', 0), (b'\xff', 255),
                         (PaddingBytes(b'x'), 120), (bytearray(b'\x80'), 128)):
                result = method(cls(b'A'), PaddingWidth(4), case[0])
                assert result.__class__ is cls
                assert len(result) == 4
                # Compare output bytes directly; padding does not depend on
                # the separate bytes.count binary-argument compatibility slice.
                filled = 0
                for index in range(len(result)):
                    if result[index] == case[1]:
                        filled += 1
                assert filled == 3
            assert method(cls(b''), 3, b'\x00') == cls(b'\x00\x00\x00')
            assert method(cls(b''), 3, b'\xff') == cls(b'\xff\xff\xff')
        # Tuple-unpacking loop targets are outside the VM's parser subset.
        for case in ((b'', b'00000'), (b'1', b'00001'),
                               (b'+1', b'+0001'), (b'-1', b'-0001'),
                               (b'+-1', b'+00-1'), (b'-', b'-0000'),
                               (b'\x00', b'0000\x00'), (b'\xff-', b'000\xff-'),
                               (b'\xe2\x88\x921', b'0\xe2\x88\x921')):
            assert cls(case[0]).zfill(5) == cls(case[1])
        source = cls(b'abc')
        result = source.ljust(1024, b'\xff')
        assert len(result) == 1024
        assert result[:3] == cls(b'abc')
        for index in range(3, 1024):
            assert result[index] == 255


def check_subclasses_and_aliases():
    for name in ('ljust', 'rjust', 'center', 'zfill'):
        method = getattr(bytes, name)
        for data in (b'', b'ab', b'+1', b'-1', b'\xff\x00'):
            source = PaddingBytes(data)
            for width in (-1, 0, 2, 6):
                result = method(source, PaddingWidth(width))
                assert result.__class__ is bytes
                assert result == method(data, width)
                assert result is not source
        # Use explicit receivers: extracted builtin methods are not bound in
        # this VM. Direct source.center(...) calls are covered separately.
        # Repeated no-op results for mutable inputs must also be independent.
        source = bytearray(b'-1')
        first = getattr(bytearray, name)(source, 0)
        second = getattr(bytearray, name)(source, 0)
        assert first is not second
        first[0] = 65
        assert second == bytearray(b'-1')
        assert source == bytearray(b'-1')
    for name in ('ljust', 'rjust', 'center'):
        for width in (0, 4):
            source = bytearray(b'\x80')
            result = getattr(bytearray, name)(source, width, source)
            expected = bytearray(b'\x80') if width == 0 else bytearray(b'\x80\x80\x80\x80')
            assert result == expected
            assert result is not source
            assert source == bytearray(b'\x80')
            source[0] = 0
            assert result == expected


def check_errors():
    for cls in (bytes, bytearray):
        for name in ('ljust', 'rjust', 'center', 'zfill'):
            method = getattr(cls, name)
            for width in (None, '3', 3.0, [], b'3'):
                expect_type_error(method, cls(b'abc'), width)
            for width in (9223372036854775808, -9223372036854775809,
                          10**100, -10**100, PaddingWidth(10**100)):
                raised = False
                try:
                    method(cls(b'abc'), width)
                except OverflowError:
                    raised = True
                assert raised
            for args in ((), (cls(b'abc'),), (1, 2), ('abc', 2),
                         (cls(b'abc'), 3, b'x', 'extra')):
                expect_type_error(method, *args)
            raised = False
            try:
                method(cls(b'abc'), width=4)
            except TypeError:
                raised = True
            assert raised
        expect_type_error(cls.zfill, cls(b'abc'), 4, b'x')
        for name in ('ljust', 'rjust', 'center'):
            method = getattr(cls, name)
            for fill in (None, 1, 'x', b'', b'xx', bytearray(),
                         bytearray(b'xx'), PaddingBytes(b''), []):
                for width in (-1, 0, 10):
                    expect_type_error(method, cls(b'abc'), width, fill)
                # Width overflow must win over an invalid fill.
                raised = False
                try:
                    method(cls(b'abc'), 10**100, fill)
                except OverflowError:
                    raised = True
                assert raised
            raised = False
            try:
                method(cls(b'abc'), 4, fillchar=b'x')
            except TypeError:
                raised = True
            assert raised


class PaddingOverride(bytes):
    def center(self, width, fill=b' '):
        # Ordinary override dispatch can reenter both builtin padding paths.
        assert bytearray(b'-1').zfill(4) == bytearray(b'-001')
        return bytes.center(self, width, fill).ljust(width + 1, b'!')


def cached_center(source, fill):
    return source.center(5, fill)


def check_calls():
    for iteration in range(32):
        assert cached_center(b'a', b'-') == b'--a--'
        assert cached_center(PaddingBytes(b'a'), PaddingBytes(b'\xff')) == b'\xff\xffa\xff\xff'
        assert cached_center(bytearray(b'a'), bytearray(b'\x00')) == bytearray(b'\x00\x00a\x00\x00')
        assert cached_center(PaddingOverride(b'a'), b'-') == b'--a--!'
        expect_type_error(cached_center, b'abcdef', b'')
        assert bytes.center(PaddingOverride(b'a'), 5, b'-') == b'--a--'


check_values()
check_subclasses_and_aliases()
check_errors()
check_calls()
