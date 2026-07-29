def assert_items(seq, *expected):
    assert len(seq) == len(expected)
    idx = 0
    while idx < len(expected):
        assert seq[idx] == expected[idx]
        idx = idx + 1


# String construction handles empty, exact-string, builtin, custom, and direct __new__ cases.
assert "abc" == "abc"
assert str() == ""

exact_string = "hello"
assert str(exact_string) is exact_string

assert str(123) == "123"
assert str(-123) == "-123"
assert str(True) == "True"


class CustomString:
    def __str__(self):
        return "custom"


assert str(CustomString()) == "custom"
assert str.__new__(str, 123) == "123"

s = "clover"
assert s[0] == "c"
assert s[1] == "l"
assert s[-1] == "r"
assert len(s[0]) == 1
assert s.__getitem__(2) == "o"
assert s.__getitem__(-1) == s[-1]

assert "Clover VM".lower() == "clover vm"
assert "Clover VM".upper() == "CLOVER VM"
assert "cLOVER".capitalize() == "Clover"
assert "Clover VM".swapcase() == "cLOVER vm"

assert "clover".startswith("cl")
assert not "clover".startswith("lo")
assert "clover".startswith("ov", start=2)
assert not "clover".startswith("ov", start=3)
assert "clover".endswith("ver")
assert not "clover".endswith("ve")
assert "clover".endswith("ov", end=4)
assert not "clover".endswith("ov", end=3)

assert "banana".find("na") == 2
assert "banana".find("zz") == -1
assert "banana".find("na", 3) == 4
assert "banana".find(sub="na", start=3) == 4
assert "banana".rfind("na") == 4
assert "banana".rfind("zz") == -1
assert "banana".rfind("") == 6
assert "banana".rfind("na", 0, 4) == 2
assert "banana".rfind(sub="na", end=4) == 2
assert "banana".index("na") == 2
assert "banana".index(sub="na", start=3) == 4
assert "banana".rindex("na") == 4
try:
    "banana".rindex("zz")
    rindex_missing_raised = False
except ValueError:
    rindex_missing_raised = True
assert rindex_missing_raised
assert "banana".count("na") == 2
assert "banana".count("") == 7
assert "banana".count("na", 3) == 1
assert "banana".count(sub="na", start=0, end=4) == 1

assert "prefix-value".removeprefix("prefix-") == "value"
assert "prefix-value".removeprefix("missing") == "prefix-value"
assert "value.txt".removesuffix(".txt") == "value"
assert "value.txt".removesuffix(".py") == "value.txt"
assert "prefix-value".removeprefix(prefix="prefix-") == "value"
assert "value.txt".removesuffix(suffix=".txt") == "value"

assert_items("a,b,c".split(","), "a", "b", "c")
assert_items("a,b,c".split(",", 1), "a", "b,c")
assert_items("a,b,c".split(sep=",", maxsplit=1), "a", "b,c")
assert_items(" a  b ".split(), "a", "b")
assert_items("".split())
assert_items("".split(","), "")
assert_items("a,b,c".rsplit(",", 1), "a,b", "c")
assert_items(" a  b ".rsplit(maxsplit=1), "a", "b")
assert_items("a,b,c".rsplit(sep=",", maxsplit=1), "a,b", "c")
assert_items("a=b=c".partition("="), "a", "=", "b=c")
assert_items("a=b=c".rpartition("="), "a=b", "=", "c")
assert_items("abc".partition("="), "abc", "", "")
assert_items("abc".rpartition("="), "", "", "abc")
assert_items("a=b".partition(sep="="), "a", "=", "b")

assert "banana".replace("na", "NA") == "baNANA"
assert "banana".replace("na", "NA", 1) == "baNAna"
assert "banana".replace("na", "NA", 0) == "banana"
assert "banana".replace("na", "NA", -2) == "baNANA"
assert "banana".replace(old="na", new="NA", count=1) == "baNAna"
assert "abc".replace("", "-") == "-a-b-c-"
assert "abc".replace("", "-", 0) == "abc"
assert "abc".replace("", "-", 1) == "-abc"
assert "abc".replace("", "-", 2) == "-a-bc"
assert "abc".replace("", "-", 4) == "-a-b-c-"
assert "abc".replace("", "-", 99) == "-a-b-c-"

assert "  hello  ".strip() == "hello"
assert "xxhellox".strip(chars="x") == "hello"
assert "...path/".strip("./") == "path"
assert "abba".strip("ab") == ""
assert "miss".strip("/") == "miss"
assert "  hello  ".lstrip() == "hello  "
assert "xxhello".lstrip("x") == "hello"
assert "xxhello".lstrip(chars="x") == "hello"
assert "  hello  ".rstrip() == "  hello"
assert "path///".rstrip("/") == "path"
assert "path///".rstrip(chars="/") == "path"
assert "///".rstrip("/") == ""
assert "miss".rstrip("/") == "miss"
assert "abcxy".rstrip("xy") == "abc"

assert ",".join(["a", "b", "c"]) == "a,b,c"
assert "::".join(("a", "b")) == "a::b"
assert ",".join([]) == ""

assert "abc".isalpha()
assert not "abc1".isalpha()
assert "123".isdigit()
assert not "12a".isdigit()
assert "abc123".isalnum()
assert not "abc 123".isalnum()
assert "".isascii()
assert "abc".isascii()
assert not chr(128).isascii()
assert "abc".islower()
assert "abc123".islower()
assert not "ABC".islower()
assert "ABC".isupper()
assert "ABC123".isupper()
assert not "abc".isupper()
assert "abc".isprintable()
assert not "a\n".isprintable()
assert " \t\n".isspace()
assert not " a ".isspace()

assert ord("A") == 65
assert chr(65) == "A"
assert ord(chr(0)) == 0

assert "same" == "same"
assert "same" != "other"
assert "same" != 4

# String comparisons and concatenation return their expected values.
assert "a" == "a"
assert not "a" == "b"
assert not "a" != "a"
assert "a" != "b"
assert "a" < "b"
assert not "b" < "a"
assert "a" <= "a"
assert not "b" <= "a"
assert "b" > "a"
assert not "a" > "b"
assert "a" >= "a"
assert not "a" >= "b"
assert "a" < "aa"
assert "ab" + "cd" == "abcd"

# Direct string dunder calls use intrinsic comparisons and return NotImplemented for wrong types.
assert "a".__eq__("a")
assert not "a".__eq__("b")
assert not "a".__ne__("a")
assert "a".__ne__("b")
assert "a".__lt__("b")
assert not "b".__lt__("a")
assert "a".__le__("a")
assert not "b".__le__("a")
assert "b".__gt__("a")
assert not "a".__gt__("b")
assert "a".__ge__("a")
assert not "a".__ge__("b")
assert "a".__lt__("aa")
assert "a".__lt__(1) is NotImplemented
assert "ab".__add__(3) is NotImplemented


def nested_string_add_wrong_type():
    return "ab".__add__(3)


assert nested_string_add_wrong_type() is NotImplemented
