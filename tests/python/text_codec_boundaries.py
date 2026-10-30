# Bounded conversion must preserve characters and absolute diagnostic offsets.
def repeat(text, count):
    parts = []
    for i in range(count):
        parts.append(text)
    return ''.join(parts)

for prefix in (4093, 4094, 4095, 4096, 4097, 8191):
    text = repeat('a', prefix) + '\U0001f600\u20ac\0end'
    data = text.encode()
    assert len(data) == prefix + 11
    assert data.decode() == text
    assert text.encode('utf8') == data
    latin = repeat('a', prefix) + '\xff\0end'
    assert latin.encode('latin1').decode('latin1') == latin

for prefix in (4095, 4096, 8191):
    text = repeat('a', prefix) + '\ud800\udfff\ud800X'
    try:
        text.encode()
        assert False
    except UnicodeEncodeError as e:
        assert e.start == prefix and e.end == prefix + 3
        assert e.object is text
    latin = repeat('a', prefix) + '\u0100\u0200\u0300X'
    try:
        latin.encode('latin1')
        assert False
    except UnicodeEncodeError as e:
        assert e.start == prefix and e.end == prefix + 3
        assert e.reason == 'ordinal not in range(256)'
    data = (repeat('a', prefix)).encode() + b'\xe2\x82'
    try:
        data.decode()
        assert False
    except UnicodeDecodeError as e:
        assert e.start == prefix and e.end == prefix + 2
        assert e.object == data

# Sources constructed in a local frame remain alive through result allocation
# and through errors; these paths are also exercised at every safepoint.
def convert_local():
    text = repeat('xyz', 7000) + '\U0001f600'
    return text.encode().decode()

assert convert_local() == repeat('xyz', 7000) + '\U0001f600'
