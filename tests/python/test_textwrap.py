"""Exercise the managed single-paragraph textwrap API."""

from textwrap import TextWrapper, fill, wrap

assert wrap("") == []
assert fill(" \t\r\n") == ""
assert wrap(text="  alpha beta", width=8,
            initial_indent=">", subsequent_indent="..") == [">  alpha", "..beta"]
assert wrap("abc", 2, initial_indent=">>", subsequent_indent="...") == [
    ">>a", "...b", "...c"
]
assert wrap("abc", 2, initial_indent=">>", subsequent_indent="..",
            drop_whitespace=False) == [">>a", "..b", "..c", ".."]
assert wrap("a", 2, initial_indent=">>", subsequent_indent="...",
            drop_whitespace=False) == [">>a", "..."]
assert wrap(" ", 1, initial_indent=">>", max_lines=2, placeholder="") == []
assert wrap("aa   bb", 4, drop_whitespace=False) == ["aa", "   ", "bb"]
assert wrap("   a", 2) == [" a"]

tabs = "a\tb\r\nc\td\ve\ff"
assert fill(tabs, tabsize=4) == "a   b  c   d e f"
assert wrap(tabs, expand_tabs=False, replace_whitespace=False) == [tabs]
assert fill(tabs, expand_tabs=False) == "a b  c d e f"
assert fill("a\tb\n\tc", tabsize=0) == "ab c"
assert fill("a\tb", expand_tabs=False, tabsize=None) == "a b"

assert wrap("a\xa0b\u2003c", 3) == ["a\xa0b", "\u2003c"]
assert wrap("\x1f\xa0\u2003") == []
assert wrap("\x1f\xa0\u2003", drop_whitespace=False) == ["\x1f\xa0\u2003"]
assert wrap("a \xa0 b") == ["a \xa0 b"]
assert wrap("a\U0001f600b", 1) == ["a", "\U0001f600", "b"]
blank_parts = []
for i in range(128):
    blank_parts.append("\xa0")
blank_run = "".join(blank_parts)
assert wrap("a " + blank_run + "x", 2) == ["a", "x"]
assert wrap("a " + blank_run + "xy" + blank_run + "z" + blank_run, 2) == [
    "a", "xy", "z\xa0"
]

assert wrap("x abcdefghi", 5) == ["x abc", "defgh", "i"]
assert wrap("x abcdefghi", 5, break_long_words=False) == ["x", "abcdefghi"]
assert wrap("x 12-34567 --1234567", 5) == ["x 12-", "34567", "--123", "4567"]
assert wrap("aa x-y a-b-c 12-34 do-not hi--there --help --alpha-beta", 5) == [
    "aa", "x-y", "a-b-c", "12-34", "do-", "not", "hi--", "there",
    "--hel", "p --a", "lpha-", "beta"
]
assert wrap("do-not hi--there", 5, break_on_hyphens=False) == [
    "do-no", "t hi-", "-ther", "e"
]
assert wrap("abc-def", 4, break_on_hyphens=1, break_long_words=False) == ["abc-def"]
assert wrap("ab-_x", 4, break_long_words=False) == ["ab-", "_x"]
# Non-ASCII characters remain intact. Hyphen classification uses ASCII.
assert wrap("\xe9\xe9-\xf6\xf6", 4, break_long_words=False) == ["\xe9\xe9-\xf6\xf6"]

assert fill('go. now! "Fine!" so? Yes.  Done.', fix_sentence_endings=True) == (
    'go.  now!  "Fine!"  so?  Yes.  Done.'
)
assert wrap("go. b", 4, fix_sentence_endings=True, drop_whitespace=False) == [
    "go.", "  b"
]
assert wrap("alpha beta gamma", 10, max_lines=1, placeholder=" ...") == ["alpha ..."]
assert wrap("one two three four", 8, max_lines=2, placeholder=" ...") == [
    "one two", "..."
]
assert wrap("abc verylongword next", 10, max_lines=2, placeholder=" ...",
            break_long_words=False) == ["abc ..."]
assert wrap("longword", 4, max_lines=1, placeholder="\xa0.",
            break_long_words=False) == ["."]
assert wrap("one two ", 7, max_lines=1, placeholder="...") == ["one two"]
assert wrap("one two ", 7, max_lines=1, placeholder="...",
            drop_whitespace=False) == ["one..."]
assert wrap("one two", 3, max_lines=0, placeholder=".") == ["."]

wrapper = TextWrapper(6, "", "-", True, True, False, True, True, True, 4)
assert wrapper.fill("alpha beta") == "alpha\n-beta"
wrapper.width = 12
wrapper.initial_indent = ">"
assert wrapper.fill(text="alpha beta") == ">alpha beta"
assert wrapper.wrap("first second text") == [">first", "-second text"]
assert wrap("one", 2**70) == ["one"]

wrapper = TextWrapper(width=0)
try:
    wrapper.wrap("")
except ValueError as error:
    assert str(error) == "invalid width 0 (must be > 0)"
else:
    assert False, "width is validated when wrapping"

try:
    wrap("", 1, max_lines=1)
except ValueError as error:
    assert str(error) == "placeholder too large for max width"
else:
    assert False, "placeholder is validated even for empty text"

try:
    TextWrapper(70, "", "", True, True, False, True, True, True, 8, 1)
except TypeError:
    pass
else:
    assert False, "max_lines is keyword-only"

try:
    fill("text", unexpected=True)
except TypeError:
    pass
else:
    assert False, "module helper forwards keyword validation"

try:
    wrap(b"text")
except TypeError:
    pass
else:
    assert False, "ordinary wrapping requires a string"
