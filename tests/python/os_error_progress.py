# Original progress-count regressions checked against CPython 3.14.5.
import errno

class Count(int):
    def __index__(self):
        raise ValueError('builtin integer payload must be used')

# Supported POSIX targets use signed 64-bit ssize_t. CloverVM's current
# sys.maxsize metadata still reports the narrower SMI range.
maximum = (1 << 63) - 1
minimum = -maximum - 1
for number in (0, 1, -2, maximum, minimum, True, False):
    e = BlockingIOError(errno.EAGAIN, 'blocked', number)
    assert e.characters_written == number
    assert e.filename is None
    assert e.args == (errno.EAGAIN, 'blocked', number)
    e = BlockingIOError(errno.EAGAIN, 'blocked', Count(number))
    assert e.characters_written == number
    assert not isinstance(e.characters_written, Count)

assert not hasattr(BlockingIOError(errno.EAGAIN, 'blocked', -1), 'characters_written')
for number in (maximum + 1, minimum - 1, 1 << 100, -(1 << 100)):
    for count in (number, Count(number)):
        try:
            BlockingIOError(errno.EAGAIN, 'blocked', count)
            assert False
        except ValueError:
            pass
        # The exact OSError constructor also selects BlockingIOError first.
        try:
            OSError(errno.EAGAIN, 'blocked', count)
            assert False
        except ValueError:
            pass
        recovered = BlockingIOError(errno.EAGAIN, 'recovered', 3)
        assert recovered.characters_written == 3
