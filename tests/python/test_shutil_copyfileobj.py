import io
import os
import shutil


events = []
failure = RuntimeError("callback failed")


class Source:
    def __init__(self, chunks, fail=None):
        self.chunks = chunks
        self.fail = fail
        self.index = 0

    @property
    def read(self):
        events.append("get read")
        if self.fail == "lookup":
            raise failure
        return self.next_chunk

    def next_chunk(self, size):
        events.append(("read", size))
        if self.fail == "call":
            raise failure
        chunk = self.chunks[self.index]
        self.index += 1
        return chunk

    def close(self):
        events.append("close source")


class Destination:
    def __init__(self, fail=None):
        self.fail = fail

    @property
    def write(self):
        events.append("get write")
        if self.fail == "lookup":
            raise failure
        return self.accept

    def accept(self, chunk):
        events.append(("write", chunk))
        if self.fail == "call":
            raise failure
        return 0

    def flush(self):
        events.append("flush destination")

    def close(self):
        events.append("close destination")


class Size:
    def __bool__(self):
        events.append("truth of length")
        return True


class FailingChunk:
    def __bool__(self):
        events.append("truth of chunk")
        raise failure


assert shutil.COPY_BUFSIZE == 256 * 1024
chunks = [bytearray(b"a"), b"bc", b"", b"unread"]
for size in (0, None, False, 9, -2):
    events = []
    assert shutil.copyfileobj(
        fdst=Destination(), length=size, fsrc=Source(chunks)
    ) is None
    requested = size if size else shutil.COPY_BUFSIZE
    assert events == ["get read", "get write", ("read", requested),
                      ("write", chunks[0]), ("read", requested),
                      ("write", chunks[1]), ("read", requested)]
    assert events[3][1] is chunks[0]

events = []
assert shutil.copyfileobj(Source([None]), Destination()) is None
assert events == ["get read", "get write", ("read", shutil.COPY_BUFSIZE)]

for case in (
    ("lookup", "lookup", ["get read"]),
    ("call", "lookup", ["get read", "get write"]),
    ("call", "call", ["get read", "get write", ("read", 1)]),
    (None, "call", ["get read", "get write", ("read", 1), ("write", b"a")]),
):
    events = []
    try:
        shutil.copyfileobj(Source([b"a"], case[0]), Destination(case[1]), 1)
        assert False
    except RuntimeError as error:
        assert error is failure
    assert events == case[2]

events = []
size = Size()
try:
    shutil.copyfileobj(Source([FailingChunk()]), Destination(), size)
    assert False
except RuntimeError as error:
    assert error is failure
assert events == ["truth of length", "get read", "get write", ("read", size),
                  "truth of chunk"]
assert events[3][1] is size

for args in ((), (Source([]),), (Source([]), Destination(), 1, 2)):
    events = []
    try:
        shutil.copyfileobj(*args)
        assert False
    except TypeError:
        pass
    assert events == []


fixture_dir = "clover-shutil-" + str(os.getpid())
src_path = fixture_dir + "/src"
dst_path = fixture_dir + "/dst"
os.mkdir(fixture_dir, 448)  # 0o700
try:
    with io.FileIO(src_path, "w+") as src, io.FileIO(dst_path, "w+") as dst:
        src.write(b"abcdef")
        src.seek(2)
        assert shutil.copyfileobj(src, dst) is None
        assert not src.closed and not dst.closed
        dst.seek(0)
        assert dst.read(10) == b"cdef"

    for size in (2, -1):
        with io.BufferedReader(io.FileIO(src_path, "r"), 16) as src:
            with io.BufferedWriter(io.FileIO(dst_path, "w+"), 16) as dst:
                assert src.read(2) == b"ab"
                assert shutil.copyfileobj(src, dst, size) is None
                assert not src.closed and not dst.closed
                raw_dst = dst.raw
                assert raw_dst.tell() == 0
                dst.flush()
                assert raw_dst.tell() == 4
                raw_dst.seek(0)
                assert raw_dst.read(10) == b"cdef"
        assert src.closed and dst.closed
finally:
    for path in (src_path, dst_path):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
    os.rmdir(fixture_dir)
