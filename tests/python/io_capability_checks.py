import io

class CapabilityError(Exception):
    pass

class BoolTrap:
    def __bool__(self):
        raise AssertionError('capability results must not be truth-tested')

class LenTrap:
    def __len__(self):
        raise AssertionError('capability results must not be truth-tested')

for base in (io.IOBase, io.RawIOBase, io.FileIO):
    class Capability(base):
        def readable(self):
            self.calls += 1
            if self.fail:
                raise CapabilityError('capability callback')
            return self.result
        writable = readable
        seekable = readable

    if base is io.FileIO:
        stream = Capability('/dev/null')
    else:
        stream = Capability()
    stream.fail = False
    # 1 compares equal to True, but must be rejected along with other results.
    for result in (True, False, 1, 0, 1.0, None, [], [1], BoolTrap(), LenTrap()):
        stream.result = result
        for check in (io.IOBase._checkReadable, io.IOBase._checkWritable,
                      io.IOBase._checkSeekable):
            stream.calls = 0
            rejected = False
            try:
                assert check(stream) is True
            except io.UnsupportedOperation:
                rejected = True
            assert rejected == (result is not True)
            assert stream.calls == 1

    # Errors raised by the capability callback itself still propagate.
    stream.fail = True
    for check in (io.IOBase._checkReadable, io.IOBase._checkWritable,
                  io.IOBase._checkSeekable):
        stream.calls = 0
        try:
            check(stream)
            assert False
        except CapabilityError as error:
            assert str(error) == 'capability callback'
        assert stream.calls == 1
    stream.close()
