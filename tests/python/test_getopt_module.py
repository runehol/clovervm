# Python test set -- getopt module
#
# Portions adapted from CPython's Lib/test/test_getopt.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import getopt
import os


def raises(error, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


def check_error(function, args, shortopts, longopts, message, opt):
    caught = raises(getopt.GetoptError, function, args, shortopts, longopts)
    assert caught.msg == message
    assert caught.opt == opt
    assert caught.args == (message, opt)
    assert str(caught) == message


def test_public_api():
    assert getopt.__all__ == ["GetoptError", "error", "getopt", "gnu_getopt"]
    assert getopt.error is getopt.GetoptError
    assert issubclass(getopt.GetoptError, Exception)
    assert getopt.GetoptError.msg == ""
    assert getopt.GetoptError.opt == ""
    error = getopt.GetoptError("bad")
    assert error.args == ("bad", "")
    assert error.msg == "bad" and error.opt == ""
    assert str(error) == "bad"
    error = getopt.GetoptError(opt="a", msg="again")
    assert error.args == ("again", "a")
    getopt.GetoptError.__init__(error, "reset", "b")
    assert error.args == ("reset", "b")
    assert error.msg == "reset" and error.opt == "b"
    assert str(error) == "reset"
    assert getopt.getopt(shortopts="a", args=["-a"]) == ([("-a", "")], [])
    assert getopt.gnu_getopt(longopts="alpha=", shortopts="", args=["--alpha=x"]) == ([("--alpha", "x")], [])
    raises(TypeError, getopt.getopt)
    raises(TypeError, getopt.gnu_getopt, [])
    raises(TypeError, getopt.getopt, [], "", [], 4)
    raises(TypeError, getopt.gnu_getopt, [], "", wrong=True)
    raises(TypeError, getopt.GetoptError)
    raises(TypeError, getopt.GetoptError, "a", "b", "c")


def test_short_options():
    for parse in (getopt.getopt, getopt.gnu_getopt):
        assert parse(["-ab", "-cfoo", "-d", "bar", "tail"], "abc:d:") == ([("-a", ""), ("-b", ""), ("-c", "foo"), ("-d", "bar")], ["tail"])
        assert parse(["-a", "", "-a", "--", "tail"], "a:") == ([("-a", ""), ("-a", "--")], ["tail"])
        assert parse(["-a=1"], "a:") == ([("-a", "=1")], [])
        assert parse(["-a", "-aon", "-ab", "tail"], "a::b") == ([("-a", ""), ("-a", "on"), ("-a", "b")], ["tail"])
        assert parse(["-\u03a9x"], "\u03a9:") == ([("-\u03a9", "x")], [])
        # Specs are interpreted on demand, including duplicate/stray colons.
        assert parse(["-a"], ":aa:") == ([("-a", "")], [])
        check_error(parse, ["-:"], ":", [], "option -: not recognized", ":")
        check_error(parse, ["-ax"], "a", [], "option -x not recognized", "x")
        check_error(parse, ["-a"], "a:a", [], "option -a requires argument", "a")


def test_long_options():
    for parse in (getopt.getopt, getopt.gnu_getopt):
        assert parse(["--alpha=2", "--beta", "--alpha", "3", "--alpha=", "tail"], "", ["alpha=", "beta"]) == ([("--alpha", "2"), ("--beta", ""), ("--alpha", "3"), ("--alpha", "")], ["tail"])
        assert parse(["--col", "--color=", "--color=off", "next"], "", ["color=?"]) == ([("--color", ""), ("--color", ""), ("--color", "off")], ["next"])
        assert parse(["--fo=1"], "", ("foo=", "far")) == ([("--foo", "1")], [])
        assert parse(["--foo"], "", ["foo", "foobar", "foo=", "foo=?"]) == ([("--foo", "")], [])
        check_error(parse, ["--foo"], "", ["foo=?", "foo="], "option --foo requires argument", "foo")
        assert parse(["--foo=42"], "", ["foo-bar", "foo="]) == ([("--foo", "42")], [])
        assert parse(["--foo", "--"], "", "foo=") == ([("--foo", "--")], [])
        assert parse(["--foo=bar=baz"], "", ["foo="]) == ([("--foo", "bar=baz")], [])
        assert parse(["--foo=arg"], "", ["foo=="]) == ([("--foo=", "arg")], [])
        assert parse(["word"], "", ["unused==", "unused=?"]) == ([], ["word"])
        check_error(parse, ["--fo"], "", ["foo=", "force"], "option --fo not a unique prefix; possible options: foo=, force", "fo")
        check_error(parse, ["--f"], "", ["foo", "foo"], "option --f not a unique prefix; possible options: foo, foo", "f")
        check_error(parse, ["--foo=x"], "", ["foo"], "option --foo must not have an argument", "foo")
        check_error(parse, ["--foo="], "", ["foo"], "option --foo must not have an argument", "foo")
        check_error(parse, ["--foo"], "", ["foo="], "option --foo requires argument", "foo")
        check_error(parse, ["--unknown"], "", [], "option --unknown not recognized", "unknown")
        check_error(parse, ["--=x"], "", ["foo", "bar"], "option -- not a unique prefix; possible options: foo, bar", "")


def test_ordering():
    args = ["x", "-a", "y", "-a", "z", "--", "end"]
    saved = args[:]
    assert getopt.getopt(args, "a") == ([], args)
    assert getopt.getopt(args, "a")[1] is args
    assert getopt.gnu_getopt(args, "a") == ([("-a", ""), ("-a", "")], ["x", "y", "z", "end"])
    assert getopt.gnu_getopt(args, "+a") == ([], args)
    grouped = getopt.gnu_getopt(args, "-a")
    assert grouped == ([(None, ["x"]), ("-a", ""), (None, ["y"]), ("-a", "")], ["z", "end"])
    assert grouped[0][0][1] is not grouped[0][2][1]
    assert args == saved
    assert getopt.gnu_getopt(["x", "y", "--long", "z"], "-", "long") == ([(None, ["x", "y"]), ("--long", "")], ["z"])
    assert getopt.gnu_getopt(["x", "--", "-a"], "-a") == ([], ["x", "-a"])
    assert getopt.getopt(["-", "-a"], "a") == ([], ["-", "-a"])
    assert getopt.gnu_getopt(["-", "-a"], "a") == ([("-a", "")], ["-"])
    for parse in (getopt.getopt, getopt.gnu_getopt):
        assert parse(["--", "-a"], "a") == ([], ["-a"])
        assert parse(["-a", "--", "-a"], "a") == ([("-a", "")], ["-a"])
        assert parse([], "") == ([], [])
    empty = []
    assert getopt.getopt(empty, "")[1] is empty
    assert getopt.gnu_getopt(empty, "")[1] is not empty
    args = ("-a", "tail")
    assert getopt.getopt(args, "a") == ([("-a", "")], ("tail",))
    assert getopt.gnu_getopt(args, "a") == ([("-a", "")], ["tail"])
    assert getopt.gnu_getopt(("x", "--", "tail"), "a") == ([], ["x", "tail"])
    assert getopt.gnu_getopt(("x", "-a"), "+a") == ([], ["x", "-a"])
    args = ("tail",)
    assert getopt.getopt(args, "a")[1] is args


class LongOptions:
    def __init__(self):
        self.calls = 0

    def __iter__(self):
        self.calls += 1
        return iter(["alpha=", "beta"])


class BrokenOptions:
    def __iter__(self):
        return self

    def __next__(self):
        raise RuntimeError("option iteration failed")


def test_iteration_and_errors():
    for parse in (getopt.getopt, getopt.gnu_getopt):
        options = LongOptions()
        assert parse(["--alpha=1", "--beta"], "", options) == ([("--alpha", "1"), ("--beta", "")], [])
        assert options.calls == 1
        error = raises(RuntimeError, parse, ["-bad"], "", BrokenOptions())
        assert str(error) == "option iteration failed"
        # Long-option iteration fails before even an empty argv is examined.
        raises(RuntimeError, parse, [], "", BrokenOptions())
        args = ["-a", "--unknown", "-b"]
        options = ["foo", "bar"]
        check_error(parse, args, "ab:", options, "option --unknown not recognized", "unknown")
        assert args == ["-a", "--unknown", "-b"]
        assert options == ["foo", "bar"]
        assert parse(["-a"], "a") == ([("-a", "")], [])
        # Non-string malformed specs fail naturally when reached, not eagerly.
        raises(AttributeError, parse, ["--foo"], "", [42])
        assert parse([], "", [42]) == ([], [])


def test_environment_value(value):
    args = ["x", "-a"]
    if value:
        assert getopt.gnu_getopt(args, "a") == ([], args)
    else:
        assert getopt.gnu_getopt(args, "a") == ([("-a", "")], ["x"])
    assert getopt.gnu_getopt(args, "+a") == ([], args)
    assert getopt.gnu_getopt(args, "-a") == ([(None, ["x"]), ("-a", "")], [])
    # Ordinary getopt never consults POSIXLY_CORRECT.
    assert getopt.getopt(["-a", "tail", "-a"], "a") == ([("-a", "")], ["tail", "-a"])


def set_environment(value):
    if hasattr(os, "environ"):
        if value is None:
            if "POSIXLY_CORRECT" in os.environ:
                del os.environ["POSIXLY_CORRECT"]
        else:
            os.environ["POSIXLY_CORRECT"] = value
    elif value is None:
        os.unsetenv("POSIXLY_CORRECT")
    else:
        os.putenv("POSIXLY_CORRECT", value)


def test_environment():
    # Restore the inherited value even if a check raises. CPython's getenv uses
    # os.environ, whereas CloverVM reads libc; isolate each process in both cases.
    saved = os.getenv("POSIXLY_CORRECT")
    try:
        set_environment(None)
        test_environment_value(None)
        set_environment("")
        test_environment_value("")
        set_environment("0")
        test_environment_value("0")
        set_environment("1")
        test_environment_value("1")
    finally:
        set_environment(saved)


def test_all():
    saved = os.getenv("POSIXLY_CORRECT")
    try:
        set_environment(None)
        test_public_api()
        test_short_options()
        test_long_options()
        test_ordering()
        test_iteration_and_errors()
        test_environment()
    finally:
        set_environment(saved)


test_all()
