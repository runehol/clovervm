# Original CPython 3.14.5 stored-payload copy regressions.
a = bytearray(b"ef")
assert bytes.__eq__(b"ab", bytearray(b"ab")) is NotImplemented
assert bytes.__ne__(b"ab", bytearray(b"ab")) is NotImplemented
assert bytes.__eq__(b"ab", bytearray(b"ac")) is NotImplemented

# Payload copies ignore subclass transformation and iteration overrides.
class StoredBytes(bytes):
    def __iter__(self):
        raise AssertionError
    def __len__(self):
        raise AssertionError
    def __getitem__(self, index):
        raise AssertionError
assert bytearray(StoredBytes(b"ab")) == b"ab"
a = bytearray()
a.extend(StoredBytes(b"cd"))
assert a == b"cd"
a[:] = StoredBytes(b"ef")
assert a == b"ef"
class BytesCopy(bytes):
    pass
copy = BytesCopy(a)
a[0] = 120
assert copy == b"ef"
