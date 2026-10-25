# Python test set -- bounded Base16/Base32 conversion
# Portions adapted from CPython Lib/test/test_base64.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
import base64
import binascii


def check_error(decode, s, message):
    caught = False
    try:
        decode(s)
    except binascii.Error as error:
        caught = True
        assert str(error) == message
        assert error.__class__ is binascii.Error
    assert caught


for decode in (base64.b32decode, base64.b32hexdecode):
    for bad in (b"A", b"AAAA", b"AAAAAAA", b"AAAAAAAAA", b"=",
                b"========", b"AAA=====", b"AAAAAA==", b"A=======",
                b"AAAAAAAA========", b"================"):
        check_error(decode, bad, "Incorrect padding")
    for bad in (b"!=======", b"AA=====A", b"=AAAAAAA", b"AA=AAAAA",
                b"AA=====\n", b"AA=====\x00", b"AA=====\xff", b"aa======"):
        check_error(decode, bad, "Non-base32 digit found")
    # Length check precedes alphabet validation; alphabet precedes pad count.
    check_error(decode, b"!", "Incorrect padding")
    check_error(decode, b"!AA=====", "Non-base32 digit found")
    for pad in range(17):
        for prefix in (b"A", b"AA", b"AAAA", b"AAAAA", b"AAAAAAA", b"AAAAAAAA"):
            if len(prefix) + pad != 8 and pad:
                # A full unpadded group followed by extra padding is invalid too.
                padding = []
                for i in range(pad):
                    padding.append(61)
                check_error(decode, prefix + bytes(padding), "Incorrect padding")
check_error(base64.b32decode, b"00======", "Non-base32 digit found")
check_error(base64.b32decode, b"11======", "Non-base32 digit found")
check_error(base64.b32hexdecode, b"WW======", "Non-base32 digit found")
check_error(base64.b32hexdecode, b"ZZ======", "Non-base32 digit found")
