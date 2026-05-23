value = 11


def read_value():
    return value


import sys

sys.provider_builtin_probe = 31
__builtins__ = sys


def read_builtin_probe():
    return provider_builtin_probe
