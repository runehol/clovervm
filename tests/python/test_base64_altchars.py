# Independently written altchars subclass regressions, checked against
# CPython 3.14.5. Kept separate to fit the VM constant-table limit.
import base64


class ReportedAltchars(bytes):
    def __bytes__(self):
        raise AssertionError("must use stored bytes")

    def __getitem__(self, key):
        raise AssertionError("must use stored bytes")

    def __len__(self):
        self.len_calls += 1
        return self.reported_length

    def __repr__(self):
        return "<altchars>"


# CPython first asserts the Python-visible length (and preserves repr in the
# assertion), then bytes.maketrans checks the actual buffer length. Neither
# translation nor that second check invokes subclass indexing/length hooks.
for raw in (b"", b"-", b"-_", b"-_x"):
    for reported in (0, 1, 2, 3):
        for call in (base64.b64encode, base64.b64decode):
            for empty in (False, True):
                alt = ReportedAltchars(raw)
                alt.reported_length = reported
                alt.len_calls = 0
                data = b""
                expected = b""
                if not empty:
                    if call is base64.b64encode:
                        data = b"\xfb\xff"
                        expected = b"-_8="
                    else:
                        data = b"-_8="
                        expected = b"\xfb\xff"
                if reported != 2:
                    caught = False
                    try:
                        call(data, alt)
                    except AssertionError as error:
                        caught = True
                        assert error.__class__ is AssertionError
                        assert str(error) == "<altchars>"
                    assert caught
                elif len(raw) != 2:
                    caught = False
                    try:
                        call(data, alt)
                    except ValueError as error:
                        caught = True
                        assert error.__class__ is ValueError
                        assert str(error) == "maketrans arguments must have same length"
                    assert caught
                else:
                    assert call(data, alt) == expected
                assert alt.len_calls == 1
