"""Common string constants.

The parsing-heavy Template and Formatter APIs are intentionally omitted until
the VM has enough formatting and protocol support to implement them honestly.
"""


ascii_lowercase = "abcdefghijklmnopqrstuvwxyz"
ascii_uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
ascii_letters = ascii_lowercase.__add__(ascii_uppercase)
digits = "0123456789"
hexdigits = digits.__add__("abcdefABCDEF")
octdigits = "01234567"
punctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
whitespace = " \t\n\r\x0b\x0c"
printable = digits.__add__(ascii_letters).__add__(punctuation).__add__(whitespace)


__all__ = (
    "ascii_letters",
    "ascii_lowercase",
    "ascii_uppercase",
    "digits",
    "hexdigits",
    "octdigits",
    "punctuation",
    "printable",
    "whitespace",
)
