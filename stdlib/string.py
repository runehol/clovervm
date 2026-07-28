"""Common string constants and helpers.

The parsing-heavy Template and Formatter APIs are intentionally omitted until the
VM has enough formatting and protocol support to implement them honestly.
"""


ascii_lowercase = "abcdefghijklmnopqrstuvwxyz"
ascii_uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
ascii_letters = ascii_lowercase + ascii_uppercase
digits = "0123456789"
hexdigits = digits + "abcdefABCDEF"
octdigits = "01234567"
punctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
whitespace = " \t\n\r\x0b\x0c"
printable = digits + ascii_letters + punctuation + whitespace


def capwords(s, sep=None):
    words = []
    if sep is None:
        for word in s.split():
            words.append(word.capitalize())
        return " ".join(words)
    for word in s.split(sep):
        words.append(word.capitalize())
    return sep.join(words)


__all__ = (
    "ascii_letters",
    "ascii_lowercase",
    "ascii_uppercase",
    "capwords",
    "digits",
    "hexdigits",
    "octdigits",
    "punctuation",
    "printable",
    "whitespace",
)
