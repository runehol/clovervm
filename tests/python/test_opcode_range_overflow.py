MAX_SMI = (1 << 58) - 1
MIN_SMI = -(1 << 58)

# Even at the positive boundary, the unit-step loop reaches its stop
# before the next cursor increment could cross the SMI range.
seen = []
for value in range(MAX_SMI - 2, MAX_SMI):
    seen.append(value)
assert seen == [MAX_SMI - 2, MAX_SMI - 1]


def direct_step(start, stop, step):
    seen = []
    for value in range(start, stop, step):
        seen.append(value)
        # Bound the failing case so a wrapped cursor cannot loop forever.
        if len(seen) > 1:
            break
    return seen


positive = direct_step(MAX_SMI - 1, MAX_SMI, 2)
negative = direct_step(MIN_SMI + 1, MIN_SMI, -2)
assert (positive, negative) == ([MAX_SMI - 1], [MIN_SMI + 1])


# The generic iterator must agree with the specialized loop opcodes.
for case in ((MAX_SMI - 1, MAX_SMI, 2), (MIN_SMI + 1, MIN_SMI, -2)):
    cursor = iter(range(case[0], case[1], case[2]))
    assert next(cursor) == case[0]
    assert next(cursor, None) is None
    assert next(cursor, None) is None
