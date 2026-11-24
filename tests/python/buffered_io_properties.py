import io


class GetterError(Exception):
    pass


class Raw(io.RawIOBase):
    def __init__(self):
        self.label = object()
        self.mode = object()
        self.action = None
        self.observed = None
        self.error = GetterError()

    @property
    def name(self):
        if self.action == 'raise':
            raise self.error
        if self.action == 'detach':
            self.action = None
            assert self.stream.detach() is self
        return self.label

    def readable(self):
        return True

    def writable(self):
        return True

    def readinto(self, dest):
        self.observe()
        return 0

    def write(self, data):
        self.observe()
        return len(data)

    def fileno(self):
        return 42

    def observe(self):
        stream = self.stream
        self.observed = (stream.raw, stream.name, stream.mode, stream.closed)


def check_unattached(stream):
    assert stream.raw is None
    for name in ('closed', 'name', 'mode'):
        try:
            getattr(stream, name)
            assert False
        except ValueError:
            pass


def check_properties(buffer_type):
    check_unattached(buffer_type.__new__(buffer_type))
    raw = Raw()
    stream = buffer_type(raw, 4)
    raw.stream = stream
    assert stream.raw is raw
    assert stream.name is raw.label
    assert stream.mode is raw.mode
    assert stream.closed is False
    for name in ('raw', 'name', 'mode', 'closed'):
        try:
            setattr(stream, name, None)
            assert False
        except AttributeError:
            pass
        try:
            delattr(stream, name)
            assert False
        except AttributeError:
            pass

    if buffer_type is io.BufferedReader:
        assert stream.read(1) == b''
    else:
        assert stream.write(b'12345') == 5
        stream.flush()
    assert raw.observed == (raw, raw.label, raw.mode, False)

    raw.action = 'raise'
    try:
        stream.name
        assert False
    except GetterError as error:
        assert error is raw.error
    raw.action = 'detach'
    assert stream.name is raw.label
    check_unattached(stream)
    assert raw.closed is False
    raw.close()

    raw = Raw()
    stream = buffer_type(raw, 4)
    with stream as entered:
        assert entered is stream
        assert io.IOBase.__enter__(stream) is stream
        assert io.IOBase._checkClosed(stream) is None
        assert io.IOBase.isatty(stream) is False
        raw.close()
        assert stream.closed is True
        assert stream.raw is raw and stream.name is raw.label
        for check in (io.IOBase.__enter__, io.IOBase._checkClosed, io.IOBase.isatty):
            try:
                check(stream)
                assert False
            except ValueError:
                pass


class QuietRaw(Raw):
    def __init__(self):
        Raw.__init__(self)
        self.close_status = []
        self.close_calls = 0

    def observe(self):
        pass

    @property
    def closed(self):
        return self.close_status

    def close(self):
        self.close_calls += 1
        self.close_status = True
        io.RawIOBase.close(self)


def check_overrides(buffer_type):
    class Overridden(buffer_type):
        @property
        def _raw(self):
            return 'subclass attribute'

        def _check_open(self):
            if isinstance(self, io.BufferedReader):
                io.BufferedReader._check_open(self)
            else:
                io.BufferedWriter._check_open(self)

        @property
        def raw(self):
            return None

        @property
        def closed(self):
            return True

        @property
        def name(self):
            return 'override'

        @property
        def mode(self):
            return 'override mode'

    raw = QuietRaw()
    stream = Overridden(raw, 4)
    assert stream._raw == 'subclass attribute'
    assert stream.fileno() == 42
    assert stream.raw is None and stream.closed is True
    assert stream.name == 'override' and stream.mode == 'override mode'
    assert buffer_type.raw.__get__(stream, Overridden) is raw
    assert buffer_type.closed.__get__(stream, Overridden) is raw.close_status
    assert buffer_type.name.__get__(stream, Overridden) is raw.label
    assert buffer_type.mode.__get__(stream, Overridden) is raw.mode
    try:
        with stream:
            assert False
    except ValueError:
        pass
    if buffer_type is io.BufferedReader:
        assert stream.read(1) == b''
    else:
        assert stream.write(b'a') == 1
    assert stream.detach() is raw
    raw.close()

    raw = QuietRaw()
    stream = Overridden(raw, 4)
    stream.close()
    assert raw.close_calls == 1 and raw.close_status is True


def check_closed_lookup(buffer_type):
    class MissingClosed(buffer_type):
        @property
        def closed(self):
            raise AttributeError('closed getter')

    stream = MissingClosed(QuietRaw(), 4)
    try:
        stream.closed
        assert False
    except AttributeError:
        pass
    assert io.IOBase._checkClosed(stream) is None
    assert io.IOBase.isatty(stream) is False
    with stream as entered:
        assert entered is stream


def check_descriptor_receivers():
    reader = io.BufferedReader(QuietRaw(), 4)
    writer = io.BufferedWriter(QuietRaw(), 4)
    for pair in ((io.BufferedReader, writer), (io.BufferedWriter, reader)):
        descriptor_class = pair[0]
        wrong_stream = pair[1]
        for name in ('raw', 'name', 'mode', 'closed'):
            descriptor = getattr(descriptor_class, name)
            for receiver in (wrong_stream, object()):
                try:
                    descriptor.__get__(receiver, descriptor_class)
                    assert False
                except TypeError:
                    pass
    reader.close()
    writer.close()


for buffer_type in (io.BufferedReader, io.BufferedWriter):
    check_properties(buffer_type)
    check_overrides(buffer_type)
    check_closed_lookup(buffer_type)
check_descriptor_receivers()
