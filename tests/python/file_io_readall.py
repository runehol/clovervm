import io
import os

path = 'clover-readall-' + str(os.getpid())
data = bytes(128 * 1024) + b'tail'
with io.FileIO(path, 'wb') as f:
    assert f.write(data) == len(data)
with io.FileIO(path) as f:
    assert f.readall() == data
    assert f.readall() == b''
    f.seek(128 * 1024)
    assert f.readall() == b'tail'
    f.seek(0)
    assert io.RawIOBase.readall(f) == data
class File(io.FileIO):
    def read(self, size=-1):
        raise AssertionError('FileIO.readall must bypass overridden read')
    def readinto(self, buffer):
        raise AssertionError('FileIO.readall must bypass overridden readinto')
with File(path) as f:
    assert f.readall() == data
    assert io.FileIO.readall(f) == b''
    try:
        io.RawIOBase.readall(f)
        assert False
    except AssertionError as error:
        assert str(error) == 'FileIO.readall must bypass overridden read'
closed = io.FileIO(path)
closed.close()
try:
    closed.readall()
    assert False
except ValueError:
    pass
with io.FileIO(path, 'wb') as f:
    try:
        f.readall()
        assert False
    except OSError:
        pass
try:
    io.FileIO.readall(io.RawIOBase())
    assert False
except TypeError:
    pass
try:
    io.FileIO.readall(object())
    assert False
except TypeError:
    pass
os.unlink(path)
