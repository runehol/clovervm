"""Lexical POSIX paths."""

__all__ = ["PurePosixPath"]


def _suffix_start(name):
    dot = name.rfind(".")
    if dot > 0 and name[:dot].lstrip("."):
        return dot
    return len(name)


def _replacement_text(value, argument):
    if not isinstance(value, str):
        raise TypeError(argument + " must be a str")
    return str.__str__(value)


def _common_prefix_length(tail, base_tail):
    limit = min(len(tail), len(base_tail))
    for index in range(limit):
        if tail[index] != base_tail[index]:
            return index
    return limit


class PurePosixPath:
    """A POSIX path supporting lexical operations without filesystem I/O."""

    def __init__(self, *segments):
        root = ""
        retained = []
        for segment in segments:
            if isinstance(segment, PurePosixPath):
                segment = segment._path
            elif not isinstance(segment, str):
                raise TypeError("path segment must be a str or PurePosixPath")

            if str.startswith(segment, "/"):
                root = "/"
                retained = [segment]
            else:
                retained.append(segment)

        if root:
            first = retained[0]
            if str.startswith(first, "//") and not str.startswith(first, "///"):
                root = "//"

        # Normalize only after the final absolute segment is known.
        parts = []
        for segment in retained:
            for part in str.split(segment, "/"):
                if part != "" and part != ".":
                    parts.append(part)

        self._path = (root + "/".join(parts)) or "."
        self._root = root
        self._tail = tuple(parts)

    def __str__(self):
        return self._path

    def joinpath(self, *pathsegments):
        return self.__class__(self, *pathsegments)

    def __truediv__(self, key):
        try:
            return self.__class__(self, key)
        except TypeError:
            return NotImplemented

    def __rtruediv__(self, key):
        try:
            return self.__class__(key, self)
        except TypeError:
            return NotImplemented

    @property
    def parts(self):
        if self.drive or self.root:
            return (self.drive + self.root,) + self._tail
        return self._tail

    @property
    def anchor(self):
        return self.drive + self.root

    @property
    def root(self):
        return self._root

    @property
    def drive(self):
        return ""

    @property
    def name(self):
        path = self._path
        if path == ".":
            return ""
        return path[path.rfind("/") + 1:]

    @property
    def parent(self):
        path = self._path
        if path == "." or path == "/" or path == "//":
            return self
        end = path.rfind("/")
        if end < 0:
            return self.__class__("")
        if end == 0 or (end == 1 and path.startswith("//")):
            end += 1
        return self.__class__(path[:end])

    @property
    def suffix(self):
        name = self.name
        return name[_suffix_start(name):]

    @property
    def suffixes(self):
        suffixes = []
        for extension in self.name.lstrip(".").split(".")[1:]:
            suffixes.append("." + extension)
        return suffixes

    @property
    def stem(self):
        name = self.name
        return name[:_suffix_start(name)]

    def with_name(self, name):
        name = _replacement_text(name, "name")
        if not name or name == "." or "/" in name:
            raise ValueError("Invalid name " + repr(name))
        if not self._tail:
            raise ValueError(repr(self._path) + " has an empty name")
        parts = self._tail[:-1] + (name,)
        return self.__class__(self._root + "/".join(parts))

    def with_stem(self, stem):
        suffix = self.suffix
        stem = _replacement_text(stem, "stem")
        if not suffix:
            return self.with_name(stem)
        if not stem:
            raise ValueError(repr(self._path) + " has a non-empty suffix")
        return self.with_name(stem + suffix)

    def with_suffix(self, suffix):
        stem = self.stem
        if not stem:
            raise ValueError(repr(self._path) + " has an empty name")
        suffix = _replacement_text(suffix, "suffix")
        if suffix and not suffix.startswith("."):
            raise ValueError("Invalid suffix " + repr(suffix))
        return self.with_name(stem + suffix)

    def is_relative_to(self, other):
        if not isinstance(other, PurePosixPath):
            other = PurePosixPath(other)
        if self._root != other._root:
            return False
        tail = self._tail
        base_tail = other._tail
        base_length = len(base_tail)
        return base_length <= len(tail) and _common_prefix_length(tail, base_tail) == base_length

    def relative_to(self, other, *, walk_up=False):
        if not isinstance(other, PurePosixPath):
            other = PurePosixPath(other)
        # Retain the original components and spelling across walk_up truth calls.
        tail = self._tail
        base_tail = other._tail
        path_text = self._path
        base_text = other._path
        common = -1
        if self._root == other._root:
            common = _common_prefix_length(tail, base_tail)
        parts = []
        # A mismatched root also leaves the anchor itself unmatched.
        for level in range(len(base_tail), common, -1):
            if not walk_up:
                raise ValueError(
                    repr(path_text) + " is not in the subpath of " + repr(base_text)
                )
            if not level:
                raise ValueError(
                    repr(path_text) + " and " + repr(base_text) + " have different anchors"
                )
            if base_tail[level - 1] == "..":
                raise ValueError("'..' segment in " + repr(base_text) + " cannot be walked")
            if common >= 0:
                parts.append("..")
        parts.extend(tail[common:])
        return self.__class__("/".join(parts))
