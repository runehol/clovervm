"""Bounded CPython 3.14.5-compatible command-line option parsing.

Supports builtin list/tuple argument sequences of strings and VM-supported
iterables of long-option strings. GNU mode reads the live process environment
through os.getenv; CPython's cached os.environ mapping is not available.
Short specs use ":" for required and "::" for optional arguments; long specs
use "=" and "=?". Optional arguments must be attached to their option.
General custom sequence/string hooks and localized diagnostics are not covered.
"""

# Portions adapted from CPython's Lib/getopt.py at v3.14.5.
# Long option support added by Lars Wirzenius; class-based exceptions by
# Gerrit Holl; gnu_getopt added by Peter Astrand.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import os

__all__ = ["GetoptError", "error", "getopt", "gnu_getopt"]


class GetoptError(Exception):
    opt = ""
    msg = ""

    def __init__(self, msg, opt=""):
        self.msg = msg
        self.opt = opt
        Exception.__init__(self, msg, opt)

    def __str__(self):
        return self.msg


error = GetoptError


def getopt(args, shortopts, longopts=[]):
    """Parse options until the first non-option; return (options, remainder)."""
    opts = []
    if isinstance(longopts, str):
        longopts = [longopts]
    else:
        longopts = list(longopts)
    while args and args[0].startswith("-") and args[0] != "-":
        if args[0] == "--":
            args = args[1:]
            break
        if args[0].startswith("--"):
            result = _do_longs(opts, args[0][2:], longopts, args[1:])
        else:
            result = _do_shorts(opts, args[0][1:], shortopts, args[1:])
        opts = result[0]
        args = result[1]
    return opts, args


def gnu_getopt(args, shortopts, longopts=[]):
    """Parse intermixed options; '+' stops early and '-' returns operand groups."""
    opts = []
    prog_args = []
    if isinstance(longopts, str):
        longopts = [longopts]
    else:
        longopts = list(longopts)

    return_in_order = False
    if shortopts.startswith("-"):
        shortopts = shortopts[1:]
        all_options_first = False
        return_in_order = True
    elif shortopts.startswith("+"):
        shortopts = shortopts[1:]
        all_options_first = True
    elif os.getenv("POSIXLY_CORRECT"):
        all_options_first = True
    else:
        all_options_first = False

    while args:
        if args[0] == "--":
            for argument in args[1:]:
                prog_args.append(argument)
            break
        if args[0][:2] == "--":
            if return_in_order and prog_args:
                opts.append((None, prog_args))
                prog_args = []
            result = _do_longs(opts, args[0][2:], longopts, args[1:])
            opts = result[0]
            args = result[1]
        elif args[0][:1] == "-" and args[0] != "-":
            if return_in_order and prog_args:
                opts.append((None, prog_args))
                prog_args = []
            result = _do_shorts(opts, args[0][1:], shortopts, args[1:])
            opts = result[0]
            args = result[1]
        elif all_options_first:
            for argument in args:
                prog_args.append(argument)
            break
        else:
            prog_args.append(args[0])
            args = args[1:]
    return opts, prog_args


def _do_longs(opts, opt, longopts, args):
    i = opt.find("=")
    if i < 0:
        optarg = None
    else:
        optarg = opt[i + 1:]
        opt = opt[:i]
    result = _long_has_args(opt, longopts)
    has_arg = result[0]
    opt = result[1]
    if has_arg:
        if optarg is None and has_arg != "?":
            if not args:
                raise GetoptError("option --" + opt + " requires argument", opt)
            optarg = args[0]
            args = args[1:]
    elif optarg is not None:
        raise GetoptError("option --" + opt + " must not have an argument", opt)
    opts.append(("--" + opt, optarg or ""))
    return opts, args


def _long_has_args(opt, longopts):
    possibilities = []
    for option in longopts:
        if option.startswith(opt):
            possibilities.append(option)
    if not possibilities:
        raise GetoptError("option --" + opt + " not recognized", opt)
    # An exact spelling wins even when several specifications share its prefix.
    if opt in possibilities:
        return False, opt
    elif opt + "=" in possibilities:
        return True, opt
    elif opt + "=?" in possibilities:
        return "?", opt
    if len(possibilities) > 1:
        raise GetoptError(
            "option --" + opt + " not a unique prefix; possible options: "
            + ", ".join(possibilities), opt
        )
    unique_match = possibilities[0]
    if unique_match.endswith("=?"):
        return "?", unique_match[:-2]
    has_arg = unique_match.endswith("=")
    if has_arg:
        unique_match = unique_match[:-1]
    return has_arg, unique_match


def _do_shorts(opts, optstring, shortopts, args):
    while optstring != "":
        opt = optstring[0]
        optstring = optstring[1:]
        has_arg = _short_has_arg(opt, shortopts)
        if has_arg:
            if optstring == "" and has_arg != "?":
                if not args:
                    raise GetoptError("option -" + opt + " requires argument", opt)
                optstring = args[0]
                args = args[1:]
            optarg = optstring
            optstring = ""
        else:
            optarg = ""
        opts.append(("-" + opt, optarg))
    return opts, args


def _short_has_arg(opt, shortopts):
    for i in range(len(shortopts)):
        if opt == shortopts[i] != ":":
            if not shortopts.startswith(":", i + 1):
                return False
            if shortopts.startswith("::", i + 1):
                return "?"
            return True
    raise GetoptError("option -" + opt + " not recognized", opt)
