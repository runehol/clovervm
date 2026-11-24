from pathlib import PurePosixPath


def check_construction():
    for input_path in ("", ".", "././"):
        path = PurePosixPath(input_path)
        assert str(path) == "."
        assert path.name == ""
        assert path.parent is path
    assert str(PurePosixPath()) == "."
    assert str(PurePosixPath("a//./b", "..", "c/")) == "a/b/../c"
    assert str(PurePosixPath("///a", "", "./b")) == "/a/b"
    assert str(PurePosixPath("ignored", "//host//./share/")) == "//host/share"
    assert str(PurePosixPath("ignored", "/..")) == "/.."
    assert str(PurePosixPath("//discard/a", "/", "b")) == "/b"
    assert str(PurePosixPath("/discard/a", PurePosixPath("//"), ".", "b/..")) == "//b/.."
    assert str(PurePosixPath("//discard/a", "///", "b")) == "/b"
    assert str(PurePosixPath("a\\b", "c:d")) == "a\\b/c:d"
    assert PurePosixPath("a\x00b").name == "a\x00b"

    class Text(str):
        pass

    assert str(PurePosixPath(Text("a//b"), Text("c"))) == "a/b/c"


def check_joining():
    base = PurePosixPath("//host/a")
    assert str(PurePosixPath(base, PurePosixPath("b/.."), "c")) == "//host/a/b/../c"
    copy = base.joinpath()
    assert copy is not base
    assert str(copy) == str(base)
    assert str(base.joinpath("b", "", PurePosixPath("./c"))) == "//host/a/b/c"
    assert str(base.joinpath("/new", "b")) == "/new/b"
    assert str(PurePosixPath("/old").joinpath("//new")) == "//new"
    assert str(base / "b") == "//host/a/b"
    assert str(base / "/new") == "/new"
    assert str(base / PurePosixPath("//new")) == "//new"
    assert str("prefix" / PurePosixPath("a")) == "prefix/a"
    assert str("prefix" / base) == "//host/a"
    assert str(base) == "//host/a"

    class Derived(PurePosixPath):
        def __init__(self, *segments):
            self.segments = segments
            PurePosixPath.__init__(self, *segments)

    derived = Derived("a/b")
    assert isinstance(derived.parent, Derived)
    assert isinstance(derived.joinpath("c"), Derived)
    assert isinstance("prefix" / derived, Derived)
    for case in (
        ("a", ""),
        ("a/b", "a"),
        ("/a", "/"),
        ("/a/b", "/a"),
        ("//a", "//"),
        ("//a/b", "//a"),
    ):
        assert Derived(case[0]).parent.segments == (case[1],)


def check_properties():
    for root in ("/", "//"):
        path = PurePosixPath(root)
        assert path.parent is path
        assert path.name == path.suffix == path.stem == ""
        assert path.suffixes == []
        assert str(PurePosixPath(path, "a").parent) == root

    path = PurePosixPath("//a/b/../")
    assert str(path.parent) == "//a/b"
    assert str(path.parent.parent) == "//a"
    assert str(PurePosixPath("..").parent) == "."
    assert str(PurePosixPath("a").parent) == "."
    assert str(PurePosixPath("a/b").parent) == "a"

    for case in (
        ("dir/plain", "plain", "", [], "plain"),
        ("dir/.profile", ".profile", "", [], ".profile"),
        ("dir/.name.tar.gz", ".name.tar.gz", ".gz", [".tar", ".gz"], ".name.tar"),
        ("dir/foo.", "foo.", ".", ["."], "foo"),
        ("dir/a..b.", "a..b.", ".", [".", ".b", "."], "a..b"),
        ("dir/...", "...", "", [], "..."),
        ("dir/..", "..", "", [], ".."),
        ("dir/\u00e9.\u03bb", "\u00e9.\u03bb", ".\u03bb", [".\u03bb"], "\u00e9"),
    ):
        path = PurePosixPath(case[0])
        assert (path.name, path.suffix, path.suffixes, path.stem) == case[1:]

    path = PurePosixPath("a.tar.gz")
    suffixes = path.suffixes
    suffixes.append(".extra")
    assert path.suffixes == [".tar", ".gz"]
    for attr in ("name", "parent", "suffix", "suffixes", "stem"):
        assert hasattr(path, attr)
        try:
            setattr(path, attr, None)
            assert False
        except AttributeError:
            pass
        try:
            delattr(path, attr)
            assert False
        except AttributeError:
            pass
    assert path.name == "a.tar.gz"


def check_invalid_calls():
    path = PurePosixPath("a")
    for invalid in (b"bytes", 1, None):
        try:
            PurePosixPath("a", invalid, "/reset")
            assert False
        except TypeError:
            pass
        try:
            path.joinpath(invalid, "/reset")
            assert False
        except TypeError:
            pass
        assert path.__truediv__(invalid) is NotImplemented
        assert path.__rtruediv__(invalid) is NotImplemented
        try:
            path / invalid
            assert False
        except TypeError:
            pass
        try:
            invalid / path
            assert False
        except TypeError:
            pass

    try:
        PurePosixPath(pathsegments="a")
        assert False
    except TypeError:
        pass
    try:
        path.joinpath(pathsegments="b")
        assert False
    except TypeError:
        pass

    class Reflected:
        def __rtruediv__(self, left):
            return left.name

    assert path / Reflected() == "a"


check_construction()
check_joining()
check_properties()
check_invalid_calls()
