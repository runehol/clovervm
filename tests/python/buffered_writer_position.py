import io
import os

path = 'clover-writer-' + str(os.getpid())
with io.FileIO(path, 'w+') as raw:
    w = io.BufferedWriter(raw, 4)
    assert w.fileno() == raw.fileno() and w.seekable()
    assert w.write(b'ab') == 2
    assert w.tell() == 2 and raw.tell() == 0
    assert w.seek(0, 1) == 2 and raw.tell() == 2
    assert w.write(b'cd') == 2
    assert w.seek(1) == 1
    assert w.write(b'Z') == 1
    detached = w.detach()
    assert detached is raw and raw.tell() == 2 and not raw.closed
    raw.seek(0)
    assert raw.readall() == b'aZcd'
    try:
        w.write(b'!')
        assert False
    except ValueError:
        pass
os.unlink(path)
