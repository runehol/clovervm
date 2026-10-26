import io

class BodyError(Exception):
    pass
class CloseError(Exception):
    pass

for base in (io.IOBase, io.RawIOBase, io.FileIO):
    class Context(base):
        def close(self):
            self.calls += 1
            return self.result
    for result in (True, False, None):
        if base is io.FileIO:
            f = Context('/dev/null')
        else:
            f = Context()
        f.calls = 0
        f.result = result
        assert io.IOBase.__exit__(f, None, None, None) is result
        assert f.calls == 1
        escaped = False
        try:
            with f:
                raise BodyError()
        except BodyError:
            escaped = True
        assert escaped == (result is not True)
        assert f.calls == 2
        if base is io.FileIO:
            io.FileIO.close(f)
        else:
            io.IOBase.close(f)

    class Raising(base):
        def close(self):
            raise CloseError()
    if base is io.FileIO:
        f = Raising('/dev/null')
    else:
        f = Raising()
    try:
        with f:
            raise BodyError()
        assert False
    except CloseError:
        pass
    if base is io.FileIO:
        io.FileIO.close(f)
    else:
        io.IOBase.close(f)

# __exit__ must return the object unchanged; with performs truth conversion.
class TruthResult:
    def __bool__(self):
        self.calls += 1
        return self.value
class ReturnsObject(io.IOBase):
    def close(self):
        return self.result
for value in (True, False):
    f = ReturnsObject()
    f.result = TruthResult()
    f.result.calls = 0
    f.result.value = value
    assert io.IOBase.__exit__(f, None, None, None) is f.result
    assert f.result.calls == 0
    escaped = False
    try:
        with f:
            raise BodyError()
    except BodyError:
        escaped = True
    assert escaped == (not value)
    assert f.result.calls == 1
    io.IOBase.close(f)
