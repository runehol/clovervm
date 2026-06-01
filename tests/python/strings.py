s = "clover"
assert s[0] == "c"
assert s[1] == "l"
assert s[-1] == "r"
assert len(s[0]) == 1
assert s.__getitem__(2) == "o"
assert s.__getitem__(-1) == s[-1]

assert "Clover VM".lower() == "clover vm"
assert "Clover VM".upper() == "CLOVER VM"

assert "clover".startswith("cl")
assert not "clover".startswith("lo")
assert "clover".endswith("ver")
assert not "clover".endswith("ve")

assert "banana".find("na") == 2
assert "banana".find("zz") == -1
assert "banana".index("na") == 2
assert "banana".count("na") == 2
assert "banana".count("") == 7

assert "banana".replace("na", "NA") == "baNANA"
assert "abc".replace("", "-") == "-a-b-c-"

assert "  hello  ".strip() == "hello"
assert "  hello  ".lstrip() == "hello  "
assert "  hello  ".rstrip() == "  hello"

assert ",".join(["a", "b", "c"]) == "a,b,c"
assert "::".join(("a", "b")) == "a::b"
assert ",".join([]) == ""

assert "abc".isalpha()
assert not "abc1".isalpha()
assert "123".isdigit()
assert not "12a".isdigit()
assert "abc123".isalnum()
assert not "abc 123".isalnum()
assert " \t\n".isspace()
assert not " a ".isspace()

assert ord("A") == 65
assert chr(65) == "A"
assert ord(chr(0)) == 0

assert "same" == "same"
assert "same" != "other"
assert "same" != 4
