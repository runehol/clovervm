# Python test set -- errno module
#
# Portions adapted from CPython's Lib/test/test_errno.py at v3.14.0.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import errno


assert isinstance(errno.errorcode, dict)
assert errno.EDOM in errno.errorcode
assert errno.ERANGE in errno.errorcode

for code in errno.errorcode:
    name = errno.errorcode[code]
    assert getattr(errno, name) == code

assert errno.errorcode[errno.ENOENT] == "ENOENT"
assert errno.errorcode.get("ENOENT") is None

if hasattr(errno, "EWOULDBLOCK"):
    alias_name = errno.errorcode[errno.EWOULDBLOCK]
    assert getattr(errno, alias_name) == errno.EWOULDBLOCK
