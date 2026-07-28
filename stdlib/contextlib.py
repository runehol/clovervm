"""Utilities for with-statement contexts.

This is a small CloverVM-supported subset. Generator-based helpers such as
``contextmanager`` are omitted until generator semantics exist.
"""

import os


class AbstractContextManager:
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return None


class closing(AbstractContextManager):
    def __init__(self, thing):
        self.thing = thing

    def __enter__(self):
        return self.thing

    def __exit__(self, exc_type, exc_value, traceback):
        self.thing.close()
        return None


class nullcontext(AbstractContextManager):
    def __init__(self, enter_result=None):
        self.enter_result = enter_result

    def __enter__(self):
        return self.enter_result

    def __exit__(self, exc_type, exc_value, traceback):
        return None


class suppress(AbstractContextManager):
    def __init__(self, *exceptions):
        self._exceptions = exceptions

    def __exit__(self, exc_type, exc_value, traceback):
        return exc_type is not None and issubclass(exc_type, self._exceptions)


class chdir(AbstractContextManager):
    def __init__(self, path):
        self.path = path
        self._old_cwd = None

    def __enter__(self):
        self._old_cwd = os.getcwd()
        os.chdir(self.path)
        return None

    def __exit__(self, exc_type, exc_value, traceback):
        os.chdir(self._old_cwd)
        return None


__all__ = (
    "AbstractContextManager",
    "chdir",
    "closing",
    "nullcontext",
    "suppress",
)
