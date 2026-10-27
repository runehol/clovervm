# Option validation order and Unicode diagnostics checked against CPython 3.14.5.
for operation in (str.encode, bytes.decode):
    source = 'x' if operation is str.encode else b'x'
    for name in ('utf\ud8008', '\ud800\udfff', '\0\ud800'):
        try:
            operation(source, name)
            assert False
        except UnicodeEncodeError as e:
            assert e.object is name
            assert e.encoding == 'utf-8'
            assert e.reason == 'surrogates not allowed'
    try:
        operation(source, '\ud800', errors=None)
        assert False
    except UnicodeEncodeError:
        pass
    try:
        operation(source, '\0', errors=None)
        assert False
    except ValueError:
        pass
    name = '\ud800'
    try:
        operation(source, errors=name)
        assert False
    except UnicodeEncodeError as e:
        assert e.object is name and e.start == 0 and e.end == 1

try:
    b''.decode('\ud800')
    assert False
except UnicodeEncodeError:
    pass

# Dots are also retried as underscores for alias-table keys.
for name in ('iso.8859_1', 'iso8859.1', 'iso.8859.1', 'iso.ir.100',
             'iso.8859.1.1987', 'ISO.8859-1', 'iso-8859.1'):
    assert '\xff'.encode(name) == b'\xff'
    assert b'\xff'.decode(name) == '\xff'
for name in ('utf8.ucs2', 'utf8.ucs4', 'UTF8.UCS4'):
    assert '\xff'.encode(name) == b'\xc3\xbf'
    assert b'\xc3\xbf'.decode(name) == '\xff'

# Canonical module names are not alias keys: don't broaden the fallback.
for name in ('utf.8', 'latin.1', 'cp.819', 'u.8', '.utf8', 'utf8.'):
    try:
        'abc'.encode(name)
        assert False
    except LookupError:
        pass
    try:
        b'abc'.decode(name)
        assert False
    except LookupError:
        pass
