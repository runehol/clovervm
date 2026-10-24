"""A CloverVM collections subset centered on UserDict.

Mapping ABCs, view set operations, fromkeys, and subclass-preserving copies
are deferred. See doc/stdlib-module-bringup-checklist.md for dependencies.
"""

# Portions adapted from CPython 3.14.5's Lib/collections/__init__.py and
# Lib/_collections_abc.py. Copyright (c) Python Software Foundation.
# Mapping mixins: Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
# The derived portions are licensed under the PSF License Agreement.


_sentinel = object()


def _pair(element):
    # The VM does not yet implement unpacking assignment. Consume exactly two
    # items, preserving the mapping mixin's TypeError/ValueError distinction.
    iterator = iter(element)
    try:
        key = next(iterator)
        value = next(iterator)
    except StopIteration:
        raise ValueError("not enough values to unpack (expected 2)")
    try:
        next(iterator)
    except StopIteration:
        return key, value
    raise ValueError("too many values to unpack (expected 2)")


class _MappingView:
    def __init__(self, mapping):
        self._mapping = mapping

    def __len__(self):
        return len(self._mapping)


class _KeysIterator:
    def __init__(self, mapping):
        self._mapping = mapping
        self._keys = iter(mapping)
        self._closed = False
        self._running = False

    def __iter__(self):
        return self

    def __next__(self):
        if self._closed:
            raise StopIteration
        if self._running:
            raise ValueError("generator already executing")
        self._running = True
        try:
            result = next(self._keys)
        except StopIteration:
            self._closed = True
            raise StopIteration
        except BaseException:
            self._closed = True
            raise
        finally:
            self._running = False
            if self._closed:
                self._mapping = None
                self._keys = None
        return result


class _KeysView(_MappingView):
    def __iter__(self):
        return _KeysIterator(self._mapping)

    def __contains__(self, key):
        return key in self._mapping

    def __repr__(self):
        return "KeysView(" + repr(self._mapping) + ")"


class _ValuesIterator:
    def __init__(self, mapping):
        self._mapping = mapping
        self._keys = iter(mapping)
        self._closed = False
        self._running = False

    def __iter__(self):
        return self

    def __next__(self):
        if self._closed:
            raise StopIteration
        if self._running:
            raise ValueError("generator already executing")
        self._running = True
        looking_up = False
        try:
            key = next(self._keys)
            looking_up = True
            result = self._mapping[key]
        except StopIteration:
            self._closed = True
            if looking_up:
                raise RuntimeError("generator raised StopIteration")
            raise StopIteration
        except BaseException:
            self._closed = True
            raise
        finally:
            self._running = False
            if self._closed:
                self._mapping = None
                self._keys = None
        return result


class _ValuesView(_MappingView):
    def __iter__(self):
        return _ValuesIterator(self._mapping)

    def __contains__(self, value):
        for key in self._mapping:
            item = self._mapping[key]
            if item is value or item == value:
                return True
        return False

    def __repr__(self):
        return "ValuesView(" + repr(self._mapping) + ")"


class _ItemsIterator:
    def __init__(self, mapping):
        self._mapping = mapping
        self._keys = iter(mapping)
        self._closed = False
        self._running = False

    def __iter__(self):
        return self

    def __next__(self):
        if self._closed:
            raise StopIteration
        if self._running:
            raise ValueError("generator already executing")
        self._running = True
        looking_up = False
        try:
            key = next(self._keys)
            looking_up = True
            result = (key, self._mapping[key])
        except StopIteration:
            self._closed = True
            if looking_up:
                raise RuntimeError("generator raised StopIteration")
            raise StopIteration
        except BaseException:
            self._closed = True
            raise
        finally:
            self._running = False
            if self._closed:
                self._mapping = None
                self._keys = None
        return result


class _ItemsView(_MappingView):
    def __iter__(self):
        return _ItemsIterator(self._mapping)

    def __contains__(self, item):
        pair = _pair(item)
        try:
            value = self._mapping[pair[0]]
        except KeyError:
            return False
        return value is pair[1] or value == pair[1]

    def __repr__(self):
        return "ItemsView(" + repr(self._mapping) + ")"


class UserDict:
    """A mutable mapping with a public backing dictionary, data."""

    __hash__ = None

    def __init__(self, dict=None, /, **kwargs):
        self.data = {}
        if dict is not None:
            self.update(dict)
        if kwargs:
            self.update(kwargs)

    def __len__(self):
        return len(self.data)

    def __getitem__(self, key):
        if key in self.data:
            return self.data[key]
        if hasattr(self.__class__, "__missing__"):
            return self.__class__.__missing__(self, key)
        raise KeyError(key)

    def __setitem__(self, key, item):
        self.data[key] = item

    def __delitem__(self, key):
        del self.data[key]

    def __contains__(self, key):
        return key in self.data

    def __iter__(self):
        # iter(dict) is unavailable in the VM; its keys view supplies the same
        # live dictionary iterator without introducing native module policy.
        return iter(self.data.keys())

    def __repr__(self):
        return repr(self.data)

    def __eq__(self, other):
        if not isinstance(other, (UserDict, dict)):
            return NotImplemented
        return dict(self.items()) == dict(other.items())

    def __ne__(self, other):
        result = self.__class__.__eq__(self, other)
        if result is NotImplemented:
            return NotImplemented
        return not result

    def get(self, key, default=None):
        if key in self:
            return self[key]
        return default

    def keys(self):
        return _KeysView(self)

    def values(self):
        return _ValuesView(self)

    def items(self):
        return _ItemsView(self)

    def update(self, other=(), /, **kwds):
        if isinstance(other, UserDict):
            for key in other:
                self[key] = other[key]
        elif hasattr(other, "keys"):
            for key in other.keys():
                self[key] = other[key]
        else:
            for element in other:
                pair = _pair(element)
                self[pair[0]] = pair[1]
        for key in kwds.keys():
            self[key] = kwds[key]

    def setdefault(self, key, default=None):
        try:
            return self[key]
        except KeyError:
            self[key] = default
        return default

    def pop(self, key, default=_sentinel):
        try:
            value = self[key]
        except KeyError:
            if default is _sentinel:
                raise
            return default
        del self[key]
        return value

    def popitem(self):
        try:
            key = next(iter(self))
        except StopIteration:
            raise KeyError
        value = self[key]
        del self[key]
        return key, value

    def clear(self):
        try:
            while True:
                self.popitem()
        except KeyError:
            pass

    def copy(self):
        if self.__class__ is not UserDict:
            raise NotImplementedError("UserDict subclass copies require copy support")
        return UserDict(self.data.copy())


__all__ = ["UserDict"]
