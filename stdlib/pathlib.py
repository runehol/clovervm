"""Lexical POSIX paths."""

__all__ = ["PurePosixPath"]


def _suffix_start(name):
    dot = name.rfind(".")
    if dot > 0 and name[:dot].lstrip("."):
        return dot
    return len(name)


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
