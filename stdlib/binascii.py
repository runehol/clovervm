"""Binary/ASCII exception shared by the bounded base64 implementation.

Conversion functions and Incomplete are not implemented yet.
"""


class Error(ValueError):
    __module__ = "binascii"
