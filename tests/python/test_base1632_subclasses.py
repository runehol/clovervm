# Bounded stored-payload contract: intentionally differs from CPython's
# Base32 subclass length/indexing and decode transformation overrides.
import base64


class StoredBytes(bytes):
    def __len__(self):
        raise AssertionError("stored payload required")

    def __getitem__(self, key):
        raise AssertionError("stored payload required")

    def __bytes__(self):
        raise AssertionError("stored payload required")

    def upper(self):
        raise AssertionError("stored payload required")

    def translate(self, *args):
        raise AssertionError("stored payload required")

    def rstrip(self, *args):
        raise AssertionError("stored payload required")


class StoredString(str):
    def __len__(self):
        raise AssertionError("stored text required")

    def __getitem__(self, key):
        raise AssertionError("stored text required")

    def encode(self, *args):
        raise AssertionError("stored text required")


for case in (
        (base64.b16encode, base64.b16decode, b"666F6F", b"666f6f"),
        (base64.b32encode, base64.b32decode, b"MZXW6===", b"mzxw6==="),
        (base64.b32hexencode, base64.b32hexdecode, b"CPNMU===", b"cpnmu===")):
    encode = case[0]
    decode = case[1]
    encoded = case[2]
    lower = case[3]
    assert encode(StoredBytes(b"foo")) == encoded
    assert decode(StoredBytes(encoded)) == b"foo"
    assert decode(StoredBytes(lower), True) == b"foo"
    assert encode(StoredBytes(b"foo")).__class__ is bytes
    assert decode(StoredBytes(encoded)).__class__ is bytes
assert base64.b16decode(StoredString("666F6F")) == b"foo"
assert base64.b32decode(StoredString("MZXW6===")) == b"foo"
assert base64.b32hexdecode(StoredString("CPNMU===")) == b"foo"


class ReportedMap(bytes):
    def __len__(self):
        self.calls += 1
        return self.reported

    def __repr__(self):
        return "<map01>"


for payload in (b"", b"L", b"IL"):
    for reported in (0, 1, 2):
        mapping = ReportedMap(payload)
        mapping.reported = reported
        mapping.calls = 0
        if reported != 1:
            caught = False
            try:
                base64.b32decode(b"", map01=mapping)
            except AssertionError as error:
                caught = True
                assert str(error) == "<map01>"
            assert caught
        elif len(payload) != 1:
            caught = False
            try:
                base64.b32decode(b"", map01=mapping)
            except ValueError as error:
                caught = True
                assert str(error) == "maketrans arguments must have same length"
            assert caught
        else:
            assert base64.b32decode(b"M1======", map01=mapping) == b"b"
        assert mapping.calls == 1
