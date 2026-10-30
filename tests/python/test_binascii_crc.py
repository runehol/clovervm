# Bounded binascii CRC tests against CPython 3.14.5.
# Incremental text fixtures adapted from CPython Lib/test/test_binascii.py
# at v3.14.5. Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Other cases are independently written, including standard check vectors.

import binascii

assert binascii.crc32(b"123456789") == 3421780262  # CRC-32/ISO-HDLC: cbf43926
assert binascii.crc_hqx(b"123456789", 0) == 12739  # CRC-16/XMODEM: 31c3
assert binascii.crc_hqx(b"123456789", 65535) == 10673  # CRC-16/IBM-3740: 29b1
assert binascii.crc32(b" this string.", binascii.crc32(b"Test the CRC-32 of")) == 1571220330
assert binascii.crc_hqx(b" this string.", binascii.crc_hqx(b"Test the CRC-32 of", 0)) == 14290


class HostileBytes(bytes):
    def __len__(self):
        raise AssertionError("length hook")
    def __getitem__(self, key):
        raise AssertionError("indexing hook")
    def __iter__(self):
        raise AssertionError("iterator hook")
    def __bytes__(self):
        raise AssertionError("conversion hook")


class HostileInt(int):
    def __index__(self):
        raise AssertionError("index hook")
    def __int__(self):
        raise AssertionError("int hook")
    def __and__(self, other):
        raise AssertionError("and hook")
    def __xor__(self, other):
        raise AssertionError("xor hook")
    def __rshift__(self, other):
        raise AssertionError("shift hook")
    def __bool__(self):
        raise AssertionError("bool hook")


# Fixture construction uses lists because bytes(range) and bytes repetition
# are outside the VM bytes constructor/operator surface.
zeros = []
ones = []
for i in range(257):
    zeros.append(0)
    ones.append(255)

# Expected values generated once with CPython 3.14.5, rather than with our CRCs.
cases = (
    (b'', -1, 4294967295, 65535),
    (b'', 1267650600228229401496703205499, 123, 123),
    (b'', -1267650600228229401496703205832, 4294966840, 65080),
    (b'a', 1, 2679148245, 32135),
    (b'\x00\xff\x80\x01', -1, 233006031, 16410),
    (b'123456789', 65536, 1837314706, 12739),
    (b'123456789', 4294967297, 3700370789, 30224),
    (b'123456789', 1267650600228229401496703205499, 3103941678, 22368),
    (b'123456789', -1267650600228229401496703205832, 3255745640, 3803),
    (bytes(list(range(256))), 0, 688229491, 32341),
    (bytes(list(range(256))), 1, 3879140792, 33541),
    (bytes(list(range(256))), -1, 3681351380, 16317),
    (bytes(ones), 0, 3013064950, 27275),
    (bytes(zeros), 65535, 885754139, 45285),
)

for case in cases:
    for data in (case[0], bytearray(case[0]), HostileBytes(case[0])):
        for seed in (case[1], HostileInt(case[1])):
            result32 = binascii.crc32(data, seed)
            result16 = binascii.crc_hqx(data, seed)
            assert result32 == case[2]
            assert result16 == case[3]
            assert result32.__class__ is int
            assert result16.__class__ is int

for seed in (False, True):
    assert binascii.crc32(b"", seed) == int(seed)
    assert binascii.crc_hqx(b"", seed) == int(seed)

# Independent bit-at-a-time oracle exercises every table entry.
def bit_crc32(byte):
    crc = 4294967295 ^ byte
    for i in range(8):
        if crc & 1:
            crc = (crc >> 1) ^ 3988292384
        else:
            crc = crc >> 1
    return crc ^ 4294967295


def bit_hqx(byte):
    crc = byte << 8
    for i in range(8):
        if crc & 32768:
            crc = (crc << 1) ^ 4129
        else:
            crc = crc << 1
    return crc & 65535


for byte in range(256):
    data = bytes([byte])
    assert binascii.crc32(data, 0) == bit_crc32(byte)
    assert binascii.crc_hqx(data, 0) == bit_hqx(byte)

# All split points, including empty chunks, and reuse of a mutable input.
data = bytearray(range(64))
for split in range(65):
    for seed in (0, -1, 1267650600228229401496703205499):
        assert binascii.crc32(data[split:], binascii.crc32(data[:split], seed)) == binascii.crc32(data, seed)
        assert binascii.crc_hqx(data[split:], binascii.crc_hqx(data[:split], seed)) == binascii.crc_hqx(data, seed)
assert data == bytearray(range(64))
data[0] = 255
assert binascii.crc32(data) == binascii.crc32(bytes(data))
assert binascii.crc_hqx(data, 0) == binascii.crc_hqx(bytes(data), 0)


def type_error(function, *args, **kwargs):
    try:
        function(*args, **kwargs)
        assert False, "expected TypeError"
    except TypeError:
        pass


type_error(binascii.crc32)
type_error(binascii.crc32, b"", 0, 1)
type_error(binascii.crc_hqx)
type_error(binascii.crc_hqx, b"")
type_error(binascii.crc_hqx, b"", 0, 1)
for function in (binascii.crc32, binascii.crc_hqx):
    type_error(function, data=b"", crc=0)
    type_error(function, b"", crc=0)
    for data in ("", "abc", [], [1, 2], (), 1, True, None):
        type_error(function, data, 0)
    for seed in (1.0, "1", b"1", None, [], ()):
        type_error(function, b"", seed)
        type_error(function, b"abc", seed)

# Returning from the native borrow must leave a bytearray freely resizable.
data = bytearray(b"a")
assert binascii.crc32(data) == binascii.crc32(b"a")
assert binascii.crc_hqx(data, 0) == binascii.crc_hqx(b"a", 0)
data.append(98)
assert binascii.crc32(data) == binascii.crc32(b"ab")
assert binascii.crc_hqx(data, 0) == binascii.crc_hqx(b"ab", 0)
