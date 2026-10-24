# CloverVM-specific JSON limits. These intentionally differ from CPython's
# configurable recursion and decimal digit limits; no CPython cases are copied.
import json


def repeat(text, count):
    parts = []
    idx = 0
    while idx < count:
        parts.append(text)
        idx += 1
    return "".join(parts)


# The ordinary comparison tests cover CPython-compatible behavior. This file
# exercises the explicit CloverVM contract only.
if hasattr(json, "_MAX_DEPTH"):
    for text in (b"1", b"{}"):
        try:
            json.loads(text)
        except TypeError:
            pass
        else:
            assert False
    limit = json._MAX_DEPTH
    nested_text = repeat("[", limit) + "0" + repeat("]", limit)
    nested = json.loads(nested_text)
    assert json.dumps(nested, separators=(",", ":")) == nested_text
    assert json.dumps(nested, check_circular=False, separators=(",", ":")) == nested_text
    assert json.loads(repeat("[", limit) + repeat("]", limit)) is not None
    for text in (repeat("[", limit + 1) + repeat("]", limit + 1),
                 repeat('{"x":', limit + 1) + "0" + repeat("}", limit + 1)):
        try:
            json.loads(text)
        except RuntimeError as error:
            assert str(error) == "Maximum JSON nesting depth exceeded"
        else:
            assert False
    obj = 0
    idx = 0
    while idx <= limit:
        obj = [obj]
        idx += 1
    try:
        json.dumps(obj)
    except RuntimeError:
        pass
    else:
        assert False
    cycle = []
    cycle.append(cycle)
    try:
        json.dumps(cycle, check_circular=False)
    except RuntimeError:
        pass
    else:
        assert False

    class Endless:
        pass

    def new_object(value):
        return Endless()

    try:
        json.dumps(Endless(), default=new_object)
    except RuntimeError:
        pass
    else:
        assert False
    # No CPython-style 4300 decimal digit cap on builtin conversion.
    digits = "1" + repeat("0", 4400)
    assert json.dumps(json.loads(digits)) == digits
    assert json.loads("0") == 0
    assert json.dumps([1]) == "[1]"

    # Exercise the approved bridge's cursor and error contracts directly.
    import _json

    class DictSubclass(dict):
        pass

    assert _json.is_exact_dict({}) == 1
    assert _json.is_exact_dict(DictSubclass()) == 0
    assert _json.is_exact_dict([]) == 0
    assert _json.dict_next({}, 0) is None
    mapping = {None: "none", "later": [1]}
    entry = _json.dict_next(mapping, 0)
    assert entry[0] == 1
    assert entry[1] is None
    assert entry[2] == "none"
    del mapping[None]
    mapping["new"] = 3
    entry = _json.dict_next(mapping, entry[0])
    assert entry[1] == "later"
    assert entry[2][0] == 1
    entry = _json.dict_next(mapping, entry[0])
    assert entry[1] == "new"
    assert entry[2] == 3
    cursor = entry[0]
    assert _json.dict_next(mapping, cursor) is None
    mapping["after_end"] = 4
    assert _json.dict_next(mapping, cursor)[1] == "after_end"
    try:
        _json.dict_next([], 0)
    except TypeError:
        pass
    else:
        assert False
    try:
        _json.dict_next({}, -1)
    except ValueError:
        pass
    else:
        assert False
    try:
        _json.dict_next({}, 1.5)
    except TypeError:
        pass
    else:
        assert False
    try:
        _json.dict_next({}, 10**30)
    except OverflowError:
        pass
    else:
        assert False
    assert json.dumps({"still": "works"}) == '{"still": "works"}'
