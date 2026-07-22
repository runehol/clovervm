# Python test set -- string module
#
# Surface selected from Python 3.14's string module help. This file does not
# copy CPython test code.

import string


assert string.ascii_lowercase == "abcdefghijklmnopqrstuvwxyz"
assert string.ascii_uppercase == "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
assert string.ascii_letters == "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
assert string.digits == "0123456789"
assert string.hexdigits == "0123456789abcdefABCDEF"
assert string.octdigits == "01234567"
assert string.whitespace == " \t\n\r\x0b\x0c"
assert string.punctuation == "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
assert string.printable == (
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    .__add__("!")
    .__add__("\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ \t\n\r\x0b\x0c")
)

assert "a" in string.ascii_lowercase
assert "Z" in string.ascii_uppercase
assert "\n" in string.whitespace
assert string.__all__[0] == "ascii_letters"
assert string.__all__[8] == "whitespace"
