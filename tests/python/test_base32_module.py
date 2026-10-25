# Python test set -- bounded Base16/Base32 conversion
# Portions adapted from CPython Lib/test/test_base64.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
import base64
import binascii


# Fixed independent vectors cover every final quantum and both alphabets.
vectors = [(b"", b"", b""),
           (b"\x00", b"AA======", b"00======"),
           (b"a", b"ME======", b"C4======"),
           (b"ab", b"MFRA====", b"C5H0===="),
           (b"abc", b"MFRGG===", b"C5H66==="),
           (b"abcd", b"MFRGGZA=", b"C5H66P0="),
           (b"abcde", b"MFRGGZDF", b"C5H66P35"),
           (b"foobar", b"MZXW6YTBOI======", b"CPNMUOJ1E8======"),
           (b"\xff\x80\x7f", b"76AH6===", b"VU07U===")]
for vector in vectors:
    raw = vector[0]
    standard = vector[1]
    extended = vector[2]
    assert base64.b32encode(raw) == standard
    assert base64.b32hexencode(raw) == extended
    assert base64.b32decode(standard) == raw
    assert base64.b32hexdecode(extended) == raw
    assert base64.b32encode(raw).__class__ is bytes
    assert base64.b32hexdecode(extended).__class__ is bytes
assert base64.b32decode(s="MFRGG===") == b"abc"
assert base64.b32hexdecode(s="C5H66===") == b"abc"
assert base64.b32encode(s=b"abc") == b"MFRGG==="
assert base64.b32hexencode(s=b"abc") == b"C5H66==="
assert base64.b32decode("mfrgg===", casefold=True) == b"abc"
assert base64.b32hexdecode("c5h66===", casefold=True) == b"abc"
assert base64.b32decode(b"me======", 1) == b"a"
assert base64.b32hexdecode(b"c4======", 1) == b"a"
# CPython accepts nonzero unused low bits.
assert base64.b32decode(b"MZ======") == b"f"
assert base64.b32hexdecode(b"CV======") == b"g"
assert base64.b32decode(b"AA======") == b"\x00"
assert base64.b32hexdecode(b"AA======") == b"R"
# Multiple full quanta followed by each final partial quantum.
values = []
for i in range(256):
    values.append(i)
raw = bytes(values)
for size in range(257):
    data = raw[:size]
    assert base64.b32decode(base64.b32encode(data)) == data
    assert base64.b32hexdecode(base64.b32hexencode(data)) == data

# Fixed CPython 3.14.5 encodings independently pin every byte value.
b32_all_bytes = (
    b'AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQTCQKRMFYYDENBWHA5DYPSAIJCEMSCKJRHFAUSUKZMFUXC6MBR' +
    b'GIZTINJWG44DSOR3HQ6T4P2AIFBEGRCFIZDUQSKKJNGE2TSPKBIVEU2UKVLFOWCZLJNVYXK6L5QGCYTD' +
    b'MRSWMZ3INFVGW3DNNZXXA4LSON2HK5TXPB4XU634PV7H7AEBQKBYJBMGQ6EITCULRSGY5D4QSGJJHFEV' +
    b'S2LZRGM2TOOJ3HU7UCQ2FI5EUWTKPKFJVKV2ZLNOV6YLDMVTWS23NN5YXG5LXPF5X274BQOCYPCMLRWH' +
    b'ZDE4VS6MZXHM7UGR2LJ5JVOW27MNTWW33TO55X7A4HROHZHF43T6R2PK5PWO33XP6DY7F47U6X3PP6HZ' +
    b'7L57Z7P674======'
)
assert base64.b32encode(raw) == b32_all_bytes
assert base64.b32decode(b32_all_bytes) == raw
b32hex_all_bytes = (
    b'000G40O40K30E209185GO38E1S8124GJ2GAHC5OO34D1M70T3OFI08924CI2A9H750KIKAPC5KN2UC1H' +
    b'68PJ8D9M6SS3IEHR7GUJSFQ085146H258P3KGIAA9D64QJIFA18L4KQKALB5EM2PB9DLONAUBTG62OJ3' +
    b'CHIMCPR8D5L6MR3DDPNN0SBIEDQ7ATJNF1SNKURSFLV7V041GA1O91C6GU48J2KBHI6OT3SGI699754L' +
    b'IQBPH6CQJEE9R7KVK2GQ58T4KMJAFA59LALQPBDELUOB3CLJMIQRDDTON6TBNF5TNQVS1GE2OF2CBHM7' +
    b'P34SLIUCPN7CVK6HQB9T9LEMQVCDJMMRRJETTNV0S7HE7P75SRJUHQFATFMERRNFU3OV5SVKUNRFFU7P' +
    b'VBTVPVFUVS======'
)
assert base64.b32hexencode(raw) == b32hex_all_bytes
assert base64.b32hexdecode(b32hex_all_bytes) == raw
