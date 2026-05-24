_recursion_limit = 1000
_switch_interval = 0.005


class _Implementation:
    pass


implementation = _Implementation()
implementation.name = "clovervm"
implementation.version = version_info
implementation.hexversion = hexversion
implementation.cache_tag = None


def getdefaultencoding():
    """Return the current default string encoding."""
    return "utf-8"


def getfilesystemencoding():
    """Return the encoding used to convert filesystem names."""
    return "utf-8"


def getfilesystemencodeerrors():
    """Return the error mode used for filesystem name conversion."""
    return "surrogateescape"


def getrecursionlimit():
    """Return the current recursion limit."""
    return _recursion_limit


def setrecursionlimit(limit):
    """Set the current recursion limit metadata value."""
    global _recursion_limit
    if limit <= 0:
        raise ValueError
    _recursion_limit = limit


def getswitchinterval():
    """Return the interpreter thread switch interval metadata value."""
    return _switch_interval


def setswitchinterval(interval):
    """Set the interpreter thread switch interval metadata value."""
    global _switch_interval
    if interval <= 0:
        raise ValueError
    _switch_interval = interval


def getsizeof(obj, *default):
    """Return the size of an object in bytes."""
    if len(default) > 1:
        raise TypeError
    if len(default) == 1:
        return default[0]
    raise TypeError


def gettrace():
    """Return the active trace function."""
    return None


def getprofile():
    """Return the active profile function."""
    return None


def is_finalizing():
    """Return True if CloverVM is shutting down."""
    return False
