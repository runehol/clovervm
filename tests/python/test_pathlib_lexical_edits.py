from pathlib import PurePosixPath


def check_error(error, method, *args, **kwargs):
    try:
        method(*args, **kwargs)
    except error as exc:
        return str(exc)
    assert False


def check_name_edits():
    path = PurePosixPath("//share/a.tar.gz")
    assert str(path.with_name(name="b")) == "//share/b"
    assert str(path.with_name("..")) == "//share/.."
    assert str(path.with_name("a\\b\x00c")) == "//share/a\\b\x00c"
    assert str(path.with_stem(stem="b")) == "//share/b.gz"
    assert str(path.with_stem(".")) == "//share/..gz"
    assert str(path.with_suffix(suffix=".zip")) == "//share/a.tar.zip"
    assert str(path.with_suffix("")) == "//share/a.tar"
    assert str(path.with_suffix(".")) == "//share/a.tar."
    assert str(path.with_suffix("..")) == "//share/a.tar.."
    assert str(path) == "//share/a.tar.gz"
    assert str(PurePosixPath("a.").with_suffix("")) == "a"
    assert str(PurePosixPath("a.").with_stem("b")) == "b."
    assert str(PurePosixPath("/a").with_suffix(".")) == "/a."
    assert str(PurePosixPath("a/.").with_stem("b")) == "b"
    assert str(PurePosixPath(".profile").with_suffix(".txt")) == ".profile.txt"
    assert str(PurePosixPath("..a.").with_suffix("")) == "..a"

    class Text(str):
        def __str__(self):
            raise AssertionError("replacement should use stored text")

    assert str(path.with_name(Text("b"))) == "//share/b"
    assert str(path.with_stem(Text("b"))) == "//share/b.gz"
    assert str(path.with_suffix(Text("."))) == "//share/a.tar."

    for invalid in ("", ".", "a/b", "/a", "a/"):
        check_error(ValueError, path.with_name, invalid)
    check_error(ValueError, path.with_stem, "")
    check_error(ValueError, path.with_stem, "a/b")
    check_error(ValueError, path.with_suffix, "zip")
    check_error(ValueError, path.with_suffix, "./zip")
    for nameless in (PurePosixPath(), PurePosixPath("/"), PurePosixPath("//")):
        assert "Invalid name" in check_error(ValueError, nameless.with_name, ".")
        assert "empty name" in check_error(ValueError, nameless.with_name, "a")
        check_error(ValueError, nameless.with_stem, "a")
        assert "empty name" in check_error(ValueError, nameless.with_suffix, "zip")

    for method in (path.with_name, path.with_stem, path.with_suffix):
        check_error(TypeError, method)
        check_error(TypeError, method, "a", "b")
        check_error(TypeError, method, b"x")


def check_relative_paths():
    for case in (
        ("a/b", "a", "b"),
        ("a/./b", "a/b/", "."),
        ("a/b", "", "a/b"),
        (".", "", "."),
        ("/", "/", "."),
        ("//", "//", "."),
        ("//share/a", "//", "share/a"),
        ("///a", "/", "a"),
        ("/a/../b", "/a", "../b"),
        ("/a/../b", "/a/..", "b"),
    ):
        path = PurePosixPath(case[0])
        base = case[1]
        result = case[2]
        assert path.is_relative_to(other=base)
        assert path.is_relative_to(PurePosixPath(base))
        assert str(path.relative_to(other=base)) == result
        assert str(path.relative_to(PurePosixPath(base), walk_up=True)) == result

    for case in (("/a", "//"), ("//a", "/"), ("a/b", "/a"), ("/a", "a")):
        path = PurePosixPath(case[0])
        base = case[1]
        assert not path.is_relative_to(base)
        check_error(ValueError, path.relative_to, base)
        assert "different anchors" in check_error(ValueError, path.relative_to, base, walk_up=True)

    for case in (
        ("a/b", "a/c/d", "../../b"),
        ("a/b", "a/b/c", ".."),
        ("a/b", "ab", "../a/b"),
        ("a/b", "A", "../a/b"),
        ("//a", "//b", "../a"),
        ("a/../x", "a/z", "../../x"),
        ("a/../x", "a/../z", "../x"),
    ):
        path = PurePosixPath(case[0])
        base = case[1]
        assert not path.is_relative_to(base)
        check_error(ValueError, path.relative_to, base)
        assert str(path.relative_to(base, walk_up=True)) == case[2]

    path = PurePosixPath("a/b")
    for base in ("a/..", "a/../c", "../a", "/a/.."):
        assert "cannot be walked" in check_error(ValueError, path.relative_to, base, walk_up=True)
        assert "subpath" in check_error(ValueError, path.relative_to, base, walk_up=False)
    check_error(TypeError, path.relative_to, "a", True)
    check_error(TypeError, path.is_relative_to, "a", "b")
    for method in (path.relative_to, path.is_relative_to):
        check_error(TypeError, method)
        check_error(TypeError, method, b"a")
        check_error(TypeError, method, None)


def check_walk_up_truth():
    class Flag:
        def __init__(self, remaining):
            self.remaining = remaining
            self.calls = 0

        def __bool__(self):
            self.calls += 1
            self.remaining -= 1
            return self.remaining >= 0

    path = PurePosixPath("a/b")
    flag = Flag(0)
    assert str(path.relative_to("a", walk_up=flag)) == "b"
    assert flag.calls == 0
    flag = Flag(2)
    assert str(path.relative_to("a/c/d", walk_up=flag)) == "../../b"
    assert flag.calls == 2
    flag = Flag(1)
    check_error(ValueError, path.relative_to, "a/c/d", walk_up=flag)
    assert flag.calls == 2
    flag = Flag(2)
    check_error(ValueError, path.relative_to, "/a", walk_up=flag)
    assert flag.calls == 2


def check_walk_up_reinitialization():
    class Flag:
        def __init__(self, target, base, allow):
            self.target = target
            self.base = base
            self.allow = allow
            self.calls = 0

        def __bool__(self):
            self.calls += 1
            if self.calls == 1:
                PurePosixPath.__init__(self.target, "/unrelated/replaced")
                PurePosixPath.__init__(self.base, "/unrelated/replaced")
            return self.allow

    path = PurePosixPath("root/one/two")
    base = PurePosixPath("root/other/still")
    flag = Flag(path, base, True)
    assert str(path.relative_to(base, walk_up=flag)) == "../../one/two"
    assert flag.calls == 2

    for case in (
        ("root/other", False, "'root/one/two' is not in the subpath of 'root/other'", 1),
        ("/root/other", True, "'root/one/two' and '/root/other' have different anchors", 3),
        ("root/other/..", True, "'..' segment in 'root/other/..' cannot be walked", 1),
    ):
        path = PurePosixPath("root/one/two")
        base = PurePosixPath(case[0])
        flag = Flag(path, base, case[1])
        assert check_error(ValueError, path.relative_to, base, walk_up=flag) == case[2]
        assert flag.calls == case[3]


def check_falsey_suffix_override():
    class EmptySuffix(str):
        def __radd__(self, stem):
            raise AssertionError("a falsey suffix should be skipped")

    class Derived(PurePosixPath):
        @property
        def suffix(self):
            return self.empty_suffix

    path = Derived("a/b")
    path.empty_suffix = EmptySuffix("")
    assert str(path.with_stem("c")) == "a/c"


def check_derived_results():
    class Derived(PurePosixPath):
        def __init__(self, *segments):
            self.segments = segments
            PurePosixPath.__init__(self, *segments)

    path = Derived("//a/b.txt")
    for case in (
        (path.with_name("c"), "//a/c"),
        (path.with_stem("c"), "//a/c.txt"),
        (path.with_suffix("."), "//a/b."),
        (path.relative_to("//a"), "b.txt"),
        (path.relative_to(path), ""),
        (path.relative_to("//a/c", walk_up=True), "../b.txt"),
    ):
        result = case[0]
        segment = case[1]
        assert isinstance(result, Derived)
        assert result.segments == (segment,)
        assert str(result) == (segment or ".")


check_name_edits()
check_relative_paths()
check_walk_up_truth()
check_walk_up_reinitialization()
check_falsey_suffix_override()
check_derived_results()
