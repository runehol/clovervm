# Independently written regressions checked against CPython 3.14.5.
import shlex


class ShortString(str):
    def __len__(self):
        return 1


class LongString(str):
    def __len__(self):
        return 1000


class FalsyString(str):
    def __len__(self):
        return 0


class NoLengthString(str):
    def __len__(self):
        raise AssertionError("split must not call the length override")


class NoIndexString(str):
    def __getitem__(self, index):
        raise AssertionError("split must not call the indexing override")


class CommentIndexString(str):
    def __getitem__(self, index):
        # In "a#skip\nb", index 2 is visited only by the comment-skipping loop.
        if index == 2:
            raise AssertionError("comment skipping must use the string payload")
        return str.__getitem__(self, index)


for cls in (ShortString, LongString, FalsyString, NoLengthString, NoIndexString):
    for posix in (False, True):
        for comments in (False, True):
            for text in ("", " \t\r\n", "one two", "'a b'c", "''\"\"", "a\\ b",
                         "caf\u00e9 \U0001f600", "a\vb\fc\u00a0d", "x\x00y",
                         "a#skip\nb", "#skip\rb", "#skip\r\nb",
                         "a#skip\n b", "a#first\n#second\nb", "'a#b' c"):
                expected = shlex.split(text, comments, posix)
                assert shlex.split(cls(text), comments, posix) == expected

            # Malformed inputs must retain their exception type and message.
            try:
                shlex.split(cls("'unterminated"), comments, posix)
                assert False
            except ValueError as error:
                assert str(error) == "No closing quotation"

    try:
        shlex.split(cls("a\\"))
        assert False
    except ValueError as error:
        assert str(error) == "No escaped character"
    assert shlex.split(cls("a\\"), posix=False) == ["a\\"]

# These exercise comment skipping independently of the ordinary character scan.
text = CommentIndexString("a#skip\nb")
assert shlex.split(text, comments=True) == ["a", "b"]
assert shlex.split(text, comments=True, posix=False) == ["ab"]
assert shlex.split(CommentIndexString("a#skip"), comments=True) == ["a"]
assert shlex.split(CommentIndexString("a#skip"), True, False) == ["a"]
